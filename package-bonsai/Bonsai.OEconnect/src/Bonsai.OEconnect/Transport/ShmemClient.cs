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
    private readonly object _cmdLock = new();

    /// <summary>Never raised: a mapped region has no connection to lose.</summary>
    public event EventHandler<string>? ConnectionLost { add { } remove { } }

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
        /* Map the WHOLE existing region (expectedSize = 0) rather than assuming
         * the default geometry — the producer's editor may have reconfigured
         * slot sizing (spec §4.7). RegionOpen then validates magic/version and
         * that the header's advertised layout actually fits what we mapped,
         * so a stale/hostile region can't hand us out-of-bounds ring offsets. */
        var rc = NativeMethods.ShmOpen(shmName, UIntPtr.Zero, out _shm, out _mapped, out _mappedSize);
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

    /* Slots consumed by the last PeekData(); 1 for an ordinary frame, more for a
     * BIT_CONTINUATION span. Reset by ConsumeData(). */
    private ulong _pendingSlots = 1;
    private byte[] _reassembly = Array.Empty<byte>();

    /// <summary>
    /// Returns one whole frame. A frame larger than a slot spans consecutive slots
    /// with BIT_CONTINUATION set (spec §4.3); this stitches them into a contiguous
    /// buffer so the rest of the stack never sees a fragment. Returns empty until
    /// every slot of the span is published — a partially-written span must never be
    /// parsed.
    /// </summary>
    public unsafe ReadOnlySpan<byte> PeekData()
    {
        if (_dataRing == IntPtr.Zero) return default;
        var ptr = NativeMethods.RingbufPeek(_dataRing, out uint slotSize);
        if (ptr == IntPtr.Zero) return default;

        _pendingSlots = 1;

        /* The header is always wholly inside the first slot. */
        if (slotSize < (uint)sizeof(OecFrameHeader))
            return new ReadOnlySpan<byte>(ptr.ToPointer(), (int)slotSize);

        var h = *(OecFrameHeader*)ptr.ToPointer();
        if ((h.Flags & OecFlags.Continuation) == 0)
            return new ReadOnlySpan<byte>(ptr.ToPointer(), (int)slotSize);

        long total = sizeof(OecFrameHeader) + (long)h.PayloadLen;
        ulong slots = (ulong)((total + slotSize - 1) / slotSize);

        /* A span longer than the producer is allowed to emit means a corrupt header;
         * drop the slot rather than trust PayloadLen. */
        if (slots > OecFlags.MaxContinuationSlots)
        {
            NativeMethods.RingbufConsume(_dataRing);
            return default;
        }

        /* Wait for the tail of the span. The producer publishes slot-by-slot, so a
         * frame can be visible before it is complete. */
        if (NativeMethods.RingbufAvailable(_dataRing) < slots) return default;

        if (_reassembly.Length < total) _reassembly = new byte[total];
        long copied = 0;
        for (ulong i = 0; i < slots; ++i)
        {
            var s = NativeMethods.RingbufPeekAt(_dataRing, i, out uint sz);
            if (s == IntPtr.Zero) return default;   // raced with an eviction
            long chunk = Math.Min(total - copied, sz);
            new ReadOnlySpan<byte>(s.ToPointer(), (int)chunk)
                .CopyTo(_reassembly.AsSpan((int)copied));
            copied += chunk;
        }

        _pendingSlots = slots;
        return _reassembly.AsSpan(0, (int)total);
    }

    public void ConsumeData()
    {
        if (_dataRing == IntPtr.Zero) return;
        NativeMethods.RingbufConsumeN(_dataRing, _pendingSlots);
        _pendingSlots = 1;
    }

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

        /* The cmd ring is single-producer, but several operators (SetTtl,
         * StartRecording, ...) share one Session and post from different Rx
         * threads. Without this lock two threads acquire the same slot and both
         * publish, corrupting the ring. Spec §5.5: one command in flight at a
         * time per Bonsai instance. */
        lock (_cmdLock)
        {
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
    }

    public void Dispose() => Stop();
}
