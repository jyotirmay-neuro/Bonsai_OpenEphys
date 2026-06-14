using System.Runtime.InteropServices;

namespace Bonsai.OEconnect.Interop;

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct OecFrameHeader
{
    public uint Magic;
    public byte VersionMajor;
    public byte VersionMinor;
    public ushort StreamId;
    public uint PayloadLen;
    public ulong SampleIndex;
    public ulong HostQpcTicks;
    public ushort Flags;
    public ushort Crc16;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct OecBlockSubheader
{
    public ushort NumChannels;
    public ushort NumSamples;
    public byte Dtype;
    public byte SourceId;
    public ushort Reserved;
}

public enum OecStatus
{
    Ok                  = 0,
    EInvalidArg         = -1,
    EBadMagic           = -2,
    EVersionMismatch    = -3,
    EFrameTooLarge      = -4,
    ERingFull           = -5,
    ERingEmpty          = -6,
    ESyscall            = -7,
    ENoSession          = -8,
    EParse              = -9,
    ENotImplemented     = -10
}

public static class OecStreams
{
    public const ushort RawBlock      = 0x0001;
    public const ushort FilteredBlock = 0x0002;
    public const ushort Spike         = 0x0003;
    public const ushort TtlEvent      = 0x0004;
    public const ushort Sync          = 0x0010;
    public const ushort Cmd           = 0x0020;
    public const ushort Ack           = 0x0021;
    public const ushort Error         = 0x0022;
    public const ushort Hello         = 0x0030;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct OecHelloBody
{
    public ushort ProtocolMajor;
    public ushort ProtocolMinor;
    public uint   PluginVersion;   // (M<<16)|(m<<8)|p
    public uint   LibVersion;
    public uint   Reserved;
}

public static class OecCmds
{
    public const ushort StartRecord = 0x0001;
    public const ushort StopRecord  = 0x0002;
    public const ushort SetTtl      = 0x0003;
    public const ushort PulseTtl    = 0x0004;
    public const ushort StartAcq    = 0x0005;
    public const ushort StopAcq     = 0x0006;
    public const ushort GetState    = 0x0007;
}

public enum OecAckStatus : ushort
{
    Ok = 0, Pending = 1, Completed = 2, Busy = 3,
    NotSupported = 4, BadArg = 5, Timeout = 6, Internal = 7
}
