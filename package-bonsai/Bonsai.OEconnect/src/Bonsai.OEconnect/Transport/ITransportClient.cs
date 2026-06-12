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

    string Name { get; }
    bool IsConnected { get; }
    ulong ProducerHeartbeatNs { get; }
}
