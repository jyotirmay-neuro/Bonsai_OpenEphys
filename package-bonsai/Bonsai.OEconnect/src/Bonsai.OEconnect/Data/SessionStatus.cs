namespace Bonsai.OEconnect.Data;

public sealed class SessionStatus
{
    public bool   IsConnected { get; init; }
    public string Transport   { get; init; } = "-";
    public string BoardName   { get; init; } = "-";
    public long   FrameCount  { get; init; }
    public long   DropCount   { get; init; }
    public double EstimatedLagMs { get; init; }

    /* Populated when the plugin sends an ERROR frame (spec §3.1). Zero / empty
     * otherwise. See OecErrors for the code values. */
    public ushort LastErrorCode { get; init; }
    public string LastErrorMessage { get; init; } = string.Empty;

    /* Remote protocol version, populated when a HELLO frame is received.
     * Falls back to v1.0 if the remote never sends HELLO within 2 s (see
     * spec/oec-protocol-v1.md §8.3). */
    public ushort RemoteProtocolMajor { get; init; }
    public ushort RemoteProtocolMinor { get; init; }
    public uint   RemotePluginVersion { get; init; }
    public uint   RemoteLibVersion    { get; init; }
    public bool   ProtocolNegotiated  { get; init; }
}

public sealed class OpenEphysConnectionException : System.Exception
{
    public string Transport { get; }
    public string Endpoint { get; }
    public long LastDropCount { get; }
    public OpenEphysConnectionException(string transport, string endpoint, long drops, string message)
        : base(message) { Transport = transport; Endpoint = endpoint; LastDropCount = drops; }
}
