using System;
using System.Collections.Concurrent;
using System.Threading;
using NetMQ;
using NetMQ.Monitoring;
using NetMQ.Sockets;

namespace Bonsai.OEconnect.Transport;

internal sealed class ZmqClient : ITransportClient
{
    /* Spec §5.4 / §5.6. */
    private const int RcvHwm = 4096;
    private const int HeartbeatIntervalMs = 500;
    private const int HeartbeatTimeoutMs = 2000;

    private SubscriberSocket? _sub;
    private RequestSocket?    _req;
    private NetMQMonitor?     _monitor;
    private Thread?           _drainer;
    private CancellationTokenSource? _cts;

    private readonly ConcurrentQueue<byte[]> _dataQ = new();
    private readonly ConcurrentQueue<byte[]> _ackQ  = new();
    private byte[]? _lastData;
    private byte[]? _lastAck;

    /* NetMQ sockets are not thread-safe, and several operators post commands from
     * different Rx threads. Spec §5.5: one command in flight at a time. */
    private readonly object _cmdLock = new();
    private int _connectionLostRaised;

    public event EventHandler<string>? ConnectionLost;

    private void RaiseConnectionLost(string reason)
    {
        /* Report once: a flapping peer must not spam OnError. */
        if (Interlocked.Exchange(ref _connectionLostRaised, 1) != 0) return;
        ConnectionLost?.Invoke(this, reason);
    }

    public string Name => "Zmq";
    public bool IsConnected => _sub != null && Volatile.Read(ref _connectionLostRaised) == 0;
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
        /* Spec §5.4: generous receive HWM so a briefly-stalled Bonsai workflow
         * doesn't make the broker drop frames on our behalf. */
        _sub.Options.ReceiveHighWatermark = RcvHwm;
        _sub.SubscribeToAnyTopic();
        _sub.Connect(pubEp);

        _req = new RequestSocket();
        ZmqSecurity.Apply(_req, curve);          // must precede Connect
        /* Spec §5.6: heartbeat the control socket so a dead peer is detected
         * rather than leaving a REQ blocked until its receive timeout. */
        _req.Options.HeartbeatInterval = TimeSpan.FromMilliseconds(HeartbeatIntervalMs);
        _req.Options.HeartbeatTimeout = TimeSpan.FromMilliseconds(HeartbeatTimeoutMs);
        _req.Connect(repEp);

        /* Spec §5.6: a control-channel disconnect surfaces as OnError upstream. */
        _monitor = new NetMQMonitor(_req, $"inproc://oec.mon.{Guid.NewGuid():N}",
                                    SocketEvents.Disconnected | SocketEvents.Closed);
        _monitor.Disconnected += (_, e) => RaiseConnectionLost($"control channel disconnected from {repEp}");
        _monitor.StartAsync();

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
        /* Stop the monitor before its socket, or it observes the close and fires. */
        if (_monitor != null) { _monitor.Stop(); _monitor.Dispose(); _monitor = null; }
        lock (_cmdLock) { _req?.Dispose(); _req = null; }
        _sub?.Dispose(); _sub = null;
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
        var payload = frameBytes.ToArray();

        /* REQ/REP is strictly synchronous and the socket is not thread-safe, so the
         * send/receive pair must be atomic with respect to other posters. */
        lock (_cmdLock)
        {
            if (_req == null) return false;
            _req.SendFrame(payload);
            if (_req.TryReceiveFrameBytes(TimeSpan.FromSeconds(2), out var reply))
            {
                _ackQ.Enqueue(reply);
                return true;
            }
        }

        /* No reply within the timeout on a heartbeated socket: the peer is gone.
         * A REQ socket that has sent without receiving cannot send again, so the
         * connection is unusable regardless. */
        RaiseConnectionLost("no ACK within 2 s on the control channel");
        return false;
    }

    public void Dispose() => Stop();
}
