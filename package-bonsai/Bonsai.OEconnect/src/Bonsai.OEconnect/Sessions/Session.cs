using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Reactive.Subjects;
using System.Threading;
using System.Threading.Tasks;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

public sealed class Session : IDisposable
{
    private readonly object _lock = new();
    private int _refCount;
    private CancellationTokenSource? _cts;
    private Thread? _reader;
    private readonly IntPtr _drift = NativeMethods.DriftCreate();

    /* Clock anchor for QPC->wall-time conversion. Populated from the first
     * SYNC frame: (qpcAnchor ticks) mapped to (utcAnchor) at Bonsai's clock,
     * with qpcFreq to scale ticks->seconds. Guarded by _clockLock. */
    private readonly object _clockLock = new();
    private bool           _haveAnchor;
    private ulong          _qpcAnchor;
    private ulong          _qpcFreq;
    private DateTimeOffset _utcAnchor;
    private int            _driftPoints;

    /* Pending command completions keyed by cookie (shmem async-ack path). */
    private readonly ConcurrentDictionary<uint, TaskCompletionSource<ushort>> _pendingAcks = new();

    public readonly Subject<RawBlock>      RawSubject      = new();
    public readonly Subject<RawBlock>      FilteredSubject = new();
    public readonly Subject<SpikeEvent>    SpikeSubject    = new();
    public readonly Subject<TtlEvent>      TtlSubject      = new();
    public readonly Subject<SyncPoint>     SyncSubject     = new();
    public readonly Subject<SessionStatus> StatusSubject   = new();

    public ITransportClient Transport { get; }
    public string Endpoint { get; }

    public long FrameCount;
    public long DropCount;

    /* HELLO negotiation state (spec v1.1 §8). */
    public ushort RemoteMajor;
    public ushort RemoteMinor;
    public uint   RemotePluginVer;
    public uint   RemoteLibVer;
    public bool   Negotiated;
    private readonly DateTime _startedAt = DateTime.UtcNow;
    private bool _v10FallbackLogged;

    public Session(ITransportClient transport, string endpoint)
    {
        Transport = transport;
        Endpoint = endpoint;
    }

    public void AddRef() => Interlocked.Increment(ref _refCount);

    public int Release() => Interlocked.Decrement(ref _refCount);

    public void StartReader()
    {
        _cts = new CancellationTokenSource();
        _reader = new Thread(() => ReaderLoop(_cts.Token))
        {
            IsBackground = true,
            Name = "OEconnect-reader"
        };
        _reader.Start();
    }

    private unsafe void ReaderLoop(CancellationToken ct)
    {
        /* Adaptive spin: keep sub-millisecond wake latency for the RT data path
         * without the ~15 ms Win32 timer quantum that Thread.Sleep(1) incurs.
         * (A named data-event wakeup, spec §4.4, is the further power optimisation.) */
        int idle = 0;
        while (!ct.IsCancellationRequested)
        {
            CheckV10Fallback();
            DrainAcks();

            var span = Transport.PeekData();
            if (span.IsEmpty)
            {
                if (++idle < 2000) Thread.SpinWait(64);
                else Thread.Yield();
                continue;
            }
            idle = 0;

            if (span.Length < sizeof(OecFrameHeader)) { Transport.ConsumeData(); continue; }
            fixed (byte* p = span)
            {
                var h = *(OecFrameHeader*)p;
                Interlocked.Increment(ref FrameCount);

                switch (h.StreamId)
                {
                    case OecStreams.RawBlock:
                    case OecStreams.FilteredBlock:
                        DispatchBlock(h, p, span.Length, h.StreamId == OecStreams.RawBlock);
                        break;
                    case OecStreams.TtlEvent:
                        DispatchTtl(h, p, span.Length);
                        break;
                    case OecStreams.Spike:
                        DispatchSpike(h, p, span.Length);
                        break;
                    case OecStreams.Sync:
                        DispatchSync(h, p, span.Length);
                        break;
                    case OecStreams.Hello:
                        DispatchHello(p, span.Length);
                        break;
                }
                if ((h.Flags & 0x0002) != 0) Interlocked.Increment(ref DropCount);
            }
            Transport.ConsumeData();
        }
    }

    /// <summary>Drain any pending ACK frames and complete waiting commands.</summary>
    private unsafe void DrainAcks()
    {
        for (;;)
        {
            var span = Transport.PeekAck();
            if (span.IsEmpty) return;
            if (span.Length >= sizeof(OecFrameHeader) + 6)
            {
                fixed (byte* p = span)
                {
                    byte* body = p + sizeof(OecFrameHeader);
                    uint cookie = *(uint*)(body + 0);
                    ushort status = *(ushort*)(body + 4);
                    if (_pendingAcks.TryRemove(cookie, out var tcs))
                        tcs.TrySetResult(status);
                }
            }
            Transport.ConsumeAck();
        }
    }

    /// <summary>Register interest in a command's ACK before posting it.</summary>
    internal Task<ushort> RegisterAck(uint cookie)
    {
        var tcs = new TaskCompletionSource<ushort>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pendingAcks[cookie] = tcs;
        return tcs.Task;
    }

    private unsafe void DispatchSync(in OecFrameHeader h, byte* p, int len)
    {
        double rate = 30000.0;
        ulong freq = 0;
        /* SYNC body (spec §3.1): {qpc_freq_hz_u64, fpga_sample_rate_hz_f64}. */
        if (len >= sizeof(OecFrameHeader) + 16)
        {
            byte* body = p + sizeof(OecFrameHeader);
            freq = *(ulong*)(body + 0);
            rate = *(double*)(body + 8);
        }

        /* Feed the drift regression from SYNC only (~1 Hz), matching the
         * 60-point / 60-second window; skip zero/degenerate stamps so HELLO or
         * ERROR frames can never poison the fit. */
        if (h.HostQpcTicks != 0)
        {
            NativeMethods.DriftAdd(_drift, h.SampleIndex, h.HostQpcTicks);
            if (++_driftPoints >= 2)
                NativeMethods.DriftFit(_drift, out _, out _);

            lock (_clockLock)
            {
                if (!_haveAnchor && freq != 0)
                {
                    _qpcAnchor = h.HostQpcTicks;
                    _qpcFreq   = freq;
                    _utcAnchor = DateTimeOffset.UtcNow;
                    _haveAnchor = true;
                }
            }
        }

        SyncSubject.OnNext(new SyncPoint {
            SampleIndex = h.SampleIndex,
            HostQpcTicks = h.HostQpcTicks,
            FpgaSampleRateHz = rate
        });
    }

    private unsafe void DispatchHello(byte* p, int len)
    {
        if (len < sizeof(OecFrameHeader) + sizeof(OecHelloBody)) return;
        var body = *(OecHelloBody*)(p + sizeof(OecFrameHeader));
        RemoteMajor     = body.ProtocolMajor;
        RemoteMinor     = body.ProtocolMinor;
        RemotePluginVer = body.PluginVersion;
        RemoteLibVer    = body.LibVersion;
        Negotiated      = true;
        if (RemoteMajor != 1)
        {
            RawSubject.OnError(new OpenEphysConnectionException(
                Transport.Name, Endpoint, 0,
                $"OE plugin emits protocol v{RemoteMajor}.{RemoteMinor}; " +
                "this Bonsai package requires v1.x."));
        }
    }

    private void CheckV10Fallback()
    {
        if (Negotiated || _v10FallbackLogged) return;
        if ((DateTime.UtcNow - _startedAt).TotalSeconds <= 2) return;
        /* Producer never sent HELLO; treat as protocol v1.0. */
        RemoteMajor = 1;
        RemoteMinor = 0;
        _v10FallbackLogged = true;
        Trace.WriteLine("[OEconnect] no HELLO within 2s -- assuming protocol v1.0");
    }

    private unsafe void DispatchBlock(in OecFrameHeader h, byte* p, int len, bool raw)
    {
        if (len < sizeof(OecFrameHeader) + sizeof(OecBlockSubheader)) return;
        var sh = *(OecBlockSubheader*)(p + sizeof(OecFrameHeader));

        /* Validate the declared geometry against the bytes we actually have.
         * NumChannels/NumSamples are attacker/garbage-reachable u16s; their
         * product can overflow int and the copy can run past the mapped slot. */
        long elems = (long)sh.NumChannels * sh.NumSamples;
        long needBytes = elems * sizeof(short);
        long avail = (long)len - sizeof(OecFrameHeader) - sizeof(OecBlockSubheader);
        if (elems < 0 || elems > int.MaxValue || needBytes > avail) return;

        var samples = new short[elems];
        if (elems > 0)
        {
            fixed (short* dst = samples)
            {
                Buffer.MemoryCopy(p + sizeof(OecFrameHeader) + sizeof(OecBlockSubheader),
                                  dst, needBytes, needBytes);
            }
        }
        var block = new RawBlock(h.SampleIndex, h.HostQpcTicks, h.StreamId,
                                 sh.NumChannels, sh.NumSamples, samples);
        (raw ? RawSubject : FilteredSubject).OnNext(block);
    }

    private unsafe void DispatchTtl(in OecFrameHeader h, byte* p, int len)
    {
        if (len < sizeof(OecFrameHeader) + 4) return;
        byte* body = p + sizeof(OecFrameHeader);
        TtlSubject.OnNext(new TtlEvent {
            SampleIndex = h.SampleIndex,
            HostQpcTicks = h.HostQpcTicks,
            Line = body[0], Edge = body[1], BoardId = body[2]
        });
    }

    private unsafe void DispatchSpike(in OecFrameHeader h, byte* p, int len)
    {
        const int HEAD = 12; // electrode_u16 + unit_u16 + threshold_f32 + waveform_len_u32
        if (len < sizeof(OecFrameHeader) + HEAD) return;
        byte* body = p + sizeof(OecFrameHeader);
        ushort electrode = *(ushort*)(body + 0);
        ushort unit = *(ushort*)(body + 2);
        float thr = *(float*)(body + 4);
        int wfBytes = len - (sizeof(OecFrameHeader) + 8);
        var wf = new short[wfBytes / sizeof(short)];
        fixed (short* dst = wf)
        {
            Buffer.MemoryCopy(body + 8, dst, wf.Length * sizeof(short), wfBytes);
        }
        SpikeSubject.OnNext(new SpikeEvent {
            SampleIndex = h.SampleIndex,
            HostQpcTicks = h.HostQpcTicks,
            ElectrodeId = electrode,
            UnitId = unit,
            Threshold = thr,
            Waveform = wf
        });
    }

    public ulong PredictQpc(ulong sampleIndex)
        => NativeMethods.DriftPredictQpc(_drift, sampleIndex);

    /// <summary>
    /// Map an FPGA sample index to host wall-clock time using the drift fit
    /// (sample -> producer QPC) plus the SYNC clock anchor (QPC -> UTC).
    /// Returns false until both a fit and an anchor are available.
    /// </summary>
    internal bool TrySampleToHostTime(ulong sampleIndex, out DateTimeOffset time)
    {
        time = default;
        ulong qpc = NativeMethods.DriftPredictQpc(_drift, sampleIndex);
        if (qpc == 0) return false;
        lock (_clockLock)
        {
            if (!_haveAnchor || _qpcFreq == 0) return false;
            /* Signed delta: samples may predict before or after the anchor. */
            double deltaSec = ((double)qpc - _qpcAnchor) / _qpcFreq;
            time = _utcAnchor.AddSeconds(deltaSec);
            return true;
        }
    }

    public void Dispose()
    {
        _cts?.Cancel();
        _reader?.Join(500);
        foreach (var kv in _pendingAcks) kv.Value.TrySetCanceled();
        _pendingAcks.Clear();
        Transport.Dispose();
        NativeMethods.DriftDestroy(_drift);
        RawSubject.OnCompleted();
        FilteredSubject.OnCompleted();
        SpikeSubject.OnCompleted();
        TtlSubject.OnCompleted();
        SyncSubject.OnCompleted();
        StatusSubject.OnCompleted();
    }
}
