using System;
using System.Collections.Concurrent;
using System.Threading;
using NetMQ;
using NetMQ.Sockets;

namespace Bonsai.OEconnect.Transport;

internal sealed class ZmqClient : ITransportClient
{
    private SubscriberSocket? _sub;
    private RequestSocket?    _req;
    private Thread?           _drainer;
    private CancellationTokenSource? _cts;

    private readonly ConcurrentQueue<byte[]> _dataQ = new();
    private readonly ConcurrentQueue<byte[]> _ackQ  = new();
    private byte[]? _lastData;
    private byte[]? _lastAck;

    public string Name => "Zmq";
    public bool IsConnected => _sub != null;
    public ulong ProducerHeartbeatNs => 0;

    public bool Start(string endpointPair)
    {
        Stop();
        var sep = endpointPair.IndexOf('|');
        if (sep < 0) return false;
        var pubEp = endpointPair.Substring(0, sep);
        var repEp = endpointPair.Substring(sep + 1);

        /* Throws for a non-loopback endpoint with no server key configured — the
         * plugin would refuse such a connection anyway, so fail loudly here rather
         * than hang on a handshake that can never complete. */
        var curve = ZmqSecurity.Resolve(pubEp, repEp);

        _sub = new SubscriberSocket();
        ZmqSecurity.Apply(_sub, curve);          // must precede Connect
        _sub.SubscribeToAnyTopic();
        _sub.Connect(pubEp);

        _req = new RequestSocket();
        ZmqSecurity.Apply(_req, curve);          // must precede Connect
        _req.Connect(repEp);

        _cts = new CancellationTokenSource();
        _drainer = new Thread(() => DrainLoop(_cts.Token)) { IsBackground = true, Name = "OEconnect-ZMQ-drain" };
        _drainer.Start();
        return true;
    }

    public void Stop()
    {
        _cts?.Cancel();
        _drainer?.Join(500);
        _drainer = null;
        _sub?.Dispose(); _sub = null;
        _req?.Dispose(); _req = null;
        _cts?.Dispose(); _cts = null;
    }

    private void DrainLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && _sub != null)
        {
            if (_sub.TryReceiveFrameBytes(TimeSpan.FromMilliseconds(50), out var topic, out bool more)
                && more
                && _sub.TryReceiveFrameBytes(TimeSpan.FromMilliseconds(50), out var body, out _))
            {
                _dataQ.Enqueue(body);
            }
        }
    }

    public ReadOnlySpan<byte> PeekData()
    {
        if (_lastData != null) return _lastData.AsSpan();
        if (_dataQ.TryDequeue(out var b)) { _lastData = b; return b; }
        return default;
    }
    public void ConsumeData() { _lastData = null; }

    public ReadOnlySpan<byte> PeekAck()
    {
        if (_lastAck != null) return _lastAck.AsSpan();
        if (_ackQ.TryDequeue(out var b)) { _lastAck = b; return b; }
        return default;
    }
    public void ConsumeAck() { _lastAck = null; }

    public bool PostCmd(ReadOnlySpan<byte> frameBytes)
    {
        if (_req == null) return false;
        _req.SendFrame(frameBytes.ToArray());
        if (_req.TryReceiveFrameBytes(TimeSpan.FromSeconds(2), out var reply))
        {
            _ackQ.Enqueue(reply);
            return true;
        }
        return false;
    }

    public void Dispose() => Stop();
}
