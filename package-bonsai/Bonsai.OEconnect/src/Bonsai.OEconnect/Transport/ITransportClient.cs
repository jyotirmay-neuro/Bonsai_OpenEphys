using System;

namespace Bonsai.OEconnect.Transport;

public interface ITransportClient : IDisposable
{
    bool Start(string endpoint);
    void Stop();

    /// <summary>Pop one data frame as a byte slice; default if none. Buffer valid until next call.</summary>
    ReadOnlySpan<byte> PeekData();
    void ConsumeData();

    ReadOnlySpan<byte> PeekAck();
    void ConsumeAck();

    /// <summary>Push a CMD frame. Returns false if the cmd ring/channel is full.</summary>
    bool PostCmd(ReadOnlySpan<byte> frameBytes);

    /// <summary>
    /// Raised when the transport observes the peer going away (spec §5.6:
    /// a control-channel disconnect must surface as an OnError on Bonsai sources).
    /// Shared memory has no connection to lose, so it never raises this.
    /// </summary>
    event EventHandler<string>? ConnectionLost;

    string Name { get; }
    bool IsConnected { get; }
    ulong ProducerHeartbeatNs { get; }
}
