using System;
using System.Runtime.InteropServices;
using Bonsai.OEconnect.Interop;

namespace Bonsai.OEconnect.Transport;

internal sealed class ShmemClient : ITransportClient
{
    private IntPtr _shm = IntPtr.Zero;
    private IntPtr _mapped = IntPtr.Zero;
    private UIntPtr _mappedSize = UIntPtr.Zero;
    private IntPtr _dataRing = IntPtr.Zero;
    private IntPtr _cmdRing  = IntPtr.Zero;
    private IntPtr _ackRing  = IntPtr.Zero;
    private IntPtr _regionHeader = IntPtr.Zero;
    private string _endpoint = string.Empty;

    public string Name => "SharedMem";
    public bool IsConnected => _shm != IntPtr.Zero;

    public ulong ProducerHeartbeatNs
    {
        get
        {
            if (_regionHeader == IntPtr.Zero) return 0;
            /* Field offset in oec_region_header_t: 36 (after slot sizes + pad). */
            return (ulong)Marshal.ReadInt64(_regionHeader, 36);
        }
    }

    public bool Start(string endpoint)
    {
        Stop();
        _endpoint = endpoint;
        const string scheme = "shm://";
        var shmName = endpoint.StartsWith(scheme, StringComparison.Ordinal)
            ? endpoint.Substring(scheme.Length)
            : endpoint;
        /* Default sizing per spec §5.7 (matches OEC_DEFAULT_* in ringbuf.h). */
        var regionSize = NativeMethods.RegionSize(
            slotSize: 65536u, slotCount: 256u,
            cmdSlotSize: 4096u, cmdSlotCount: 64u,
            ackSlotSize: 4096u, ackSlotCount: 64u);
        if (regionSize == UIntPtr.Zero) return false;
        var rc = NativeMethods.ShmOpen(shmName, regionSize, out _shm, out _mapped, out _mappedSize);
        if (rc != OecStatus.Ok) return false;
        if (NativeMethods.RegionOpen(_mapped, _mappedSize, out _regionHeader) != OecStatus.Ok) { Stop(); return false; }
        if (NativeMethods.RingbufAttach(_mapped, 0, out _dataRing) != OecStatus.Ok) { Stop(); return false; }
        if (NativeMethods.RingbufAttach(_mapped, 1, out _cmdRing)  != OecStatus.Ok) { Stop(); return false; }
        if (NativeMethods.RingbufAttach(_mapped, 2, out _ackRing)  != OecStatus.Ok) { Stop(); return false; }
        return true;
    }

    public void Stop()
    {
        if (_dataRing != IntPtr.Zero) { NativeMethods.RingbufDetach(_dataRing); _dataRing = IntPtr.Zero; }
        if (_cmdRing  != IntPtr.Zero) { NativeMethods.RingbufDetach(_cmdRing);  _cmdRing  = IntPtr.Zero; }
        if (_ackRing  != IntPtr.Zero) { NativeMethods.RingbufDetach(_ackRing);  _ackRing  = IntPtr.Zero; }
        if (_shm != IntPtr.Zero)      { NativeMethods.ShmClose(_shm);            _shm = IntPtr.Zero; }
        _mapped = IntPtr.Zero; _mappedSize = UIntPtr.Zero; _regionHeader = IntPtr.Zero;
    }

    public ReadOnlySpan<byte> PeekData()
    {
        if (_dataRing == IntPtr.Zero) return default;
        var ptr = NativeMethods.RingbufPeek(_dataRing, out uint size);
        if (ptr == IntPtr.Zero) return default;
        unsafe { return new ReadOnlySpan<byte>(ptr.ToPointer(), (int)size); }
    }
    public void ConsumeData() { if (_dataRing != IntPtr.Zero) NativeMethods.RingbufConsume(_dataRing); }

    public ReadOnlySpan<byte> PeekAck()
    {
        if (_ackRing == IntPtr.Zero) return default;
        var ptr = NativeMethods.RingbufPeek(_ackRing, out uint size);
        if (ptr == IntPtr.Zero) return default;
        unsafe { return new ReadOnlySpan<byte>(ptr.ToPointer(), (int)size); }
    }
    public void ConsumeAck() { if (_ackRing != IntPtr.Zero) NativeMethods.RingbufConsume(_ackRing); }

    public bool PostCmd(ReadOnlySpan<byte> frameBytes)
    {
        if (_cmdRing == IntPtr.Zero) return false;
        var slot = NativeMethods.RingbufAcquire(_cmdRing, 0, out uint cap);
        if (slot == IntPtr.Zero || cap < frameBytes.Length) return false;
        unsafe
        {
            var dst = new Span<byte>(slot.ToPointer(), (int)cap);
            frameBytes.CopyTo(dst);
        }
        NativeMethods.RingbufPublish(_cmdRing);
        return true;
    }

    public void Dispose() => Stop();
}
