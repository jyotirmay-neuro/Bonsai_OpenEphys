using System;
using System.Runtime.InteropServices;

namespace Bonsai.OEconnect.Interop;

internal static class NativeMethods
{
    private const string Lib = "oeconnect";

    /* --- frame --- */
    [DllImport(Lib, EntryPoint = "oec_frame_init", CallingConvention = CallingConvention.Cdecl)]
    public static extern void FrameInit(ref OecFrameHeader header,
        ushort streamId, uint payloadLen, ulong sampleIndex,
        ulong hostQpcTicks, ushort flags);

    [DllImport(Lib, EntryPoint = "oec_frame_validate", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus FrameValidate(in OecFrameHeader header);

    [DllImport(Lib, EntryPoint = "oec_crc16", CallingConvention = CallingConvention.Cdecl)]
    public static extern ushort Crc16(IntPtr data, UIntPtr len);

    /* --- region --- */
    [DllImport(Lib, EntryPoint = "oec_region_size", CallingConvention = CallingConvention.Cdecl)]
    public static extern UIntPtr RegionSize(
        uint slotSize, uint slotCount,
        uint cmdSlotSize, uint cmdSlotCount,
        uint ackSlotSize, uint ackSlotCount);

    [DllImport(Lib, EntryPoint = "oec_region_open", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus RegionOpen(IntPtr mem, UIntPtr memLen, out IntPtr outHeader);

    [DllImport(Lib, EntryPoint = "oec_region_init", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus RegionInit(IntPtr mem, UIntPtr memLen,
        uint slotSize, uint slotCount,
        uint cmdSlotSize, uint cmdSlotCount,
        uint ackSlotSize, uint ackSlotCount);

    /* --- ringbuf --- */
    [DllImport(Lib, EntryPoint = "oec_ringbuf_attach", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus RingbufAttach(IntPtr regionMem, int kind, out IntPtr outRing);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_detach", CallingConvention = CallingConvention.Cdecl)]
    public static extern void RingbufDetach(IntPtr rb);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_peek", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr RingbufPeek(IntPtr rb, out uint outSize);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_consume", CallingConvention = CallingConvention.Cdecl)]
    public static extern void RingbufConsume(IntPtr rb);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_acquire", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr RingbufAcquire(IntPtr rb, int dropOldest, out uint outSize);

    [DllImport(Lib, EntryPoint = "oec_ringbuf_publish", CallingConvention = CallingConvention.Cdecl)]
    public static extern void RingbufPublish(IntPtr rb);

    /* --- shm --- */
    [DllImport(Lib, EntryPoint = "oec_shm_open", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern OecStatus ShmOpen(
        [MarshalAs(UnmanagedType.LPStr)] string name, UIntPtr expectedSize,
        out IntPtr outShm, out IntPtr outMapped, out UIntPtr outMappedSize);

    [DllImport(Lib, EntryPoint = "oec_shm_create", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern OecStatus ShmCreate(
        [MarshalAs(UnmanagedType.LPStr)] string name, UIntPtr sizeBytes, int truncate,
        out IntPtr outShm, out IntPtr outMapped, out UIntPtr outMappedSize);

    [DllImport(Lib, EntryPoint = "oec_shm_close", CallingConvention = CallingConvention.Cdecl)]
    public static extern void ShmClose(IntPtr shm);

    [DllImport(Lib, EntryPoint = "oec_shm_unlink", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern OecStatus ShmUnlink([MarshalAs(UnmanagedType.LPStr)] string name);

    /* --- drift --- */
    [DllImport(Lib, EntryPoint = "oec_drift_create", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr DriftCreate();

    [DllImport(Lib, EntryPoint = "oec_drift_destroy", CallingConvention = CallingConvention.Cdecl)]
    public static extern void DriftDestroy(IntPtr fit);

    [DllImport(Lib, EntryPoint = "oec_drift_add", CallingConvention = CallingConvention.Cdecl)]
    public static extern void DriftAdd(IntPtr fit, ulong sampleIndex, ulong qpc);

    [DllImport(Lib, EntryPoint = "oec_drift_fit", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus DriftFit(IntPtr fit, out double outA, out double outB);

    [DllImport(Lib, EntryPoint = "oec_drift_predict_qpc", CallingConvention = CallingConvention.Cdecl)]
    public static extern ulong DriftPredictQpc(IntPtr fit, ulong sampleIndex);

    /// <summary>Residual RMS of the current fit, in QPC ticks; -1 if unfitted.</summary>
    [DllImport(Lib, EntryPoint = "oec_drift_residual_rms", CallingConvention = CallingConvention.Cdecl)]
    public static extern double DriftResidualRms(IntPtr fit);

    [DllImport(Lib, EntryPoint = "oec_drift_reset", CallingConvention = CallingConvention.Cdecl)]
    public static extern void DriftReset(IntPtr fit);

    /* --- sidecar --- */
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct OecSidecar
    {
        public int Pid;
        public int NodeId;   /* OE processor node id; scopes the session */
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ShmRegion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string DataEvent;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string CmdEvent;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ZmqFallbackEndpoint;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ZmqCmdEndpoint;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)] public string SpecVersion;
        public ulong StartedUnixNs;
    }

    [DllImport(Lib, EntryPoint = "oec_sidecar_read", CallingConvention = CallingConvention.Cdecl)]
    public static extern OecStatus SidecarRead(int pid, int nodeId, out OecSidecar outSidecar);

    [DllImport(Lib, EntryPoint = "oec_sidecar_dir", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern OecStatus SidecarDir(
        [Out] byte[] outBuf, UIntPtr outBufLen);
}
