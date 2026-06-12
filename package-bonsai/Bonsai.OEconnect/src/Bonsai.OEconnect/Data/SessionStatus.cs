namespace Bonsai.OEconnect.Data;

public sealed class SessionStatus
{
    public bool   IsConnected { get; init; }
    public string Transport   { get; init; } = "-";
    public string BoardName   { get; init; } = "-";
    public long   FrameCount  { get; init; }
    public long   DropCount   { get; init; }
    public double EstimatedLagMs { get; init; }
}

public sealed class OpenEphysConnectionException : System.Exception
{
    public string Transport { get; }
    public string Endpoint { get; }
    public long LastDropCount { get; }
    public OpenEphysConnectionException(string transport, string endpoint, long drops, string message)
        : base(message) { Transport = transport; Endpoint = endpoint; LastDropCount = drops; }
}
