using System;
using System.Reactive.Subjects;
using System.Threading;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

internal sealed class Session : IDisposable
{
    private readonly object _lock = new();
    private int _refCount;
    private CancellationTokenSource? _cts;
    private Thread? _reader;
    private readonly IntPtr _drift = NativeMethods.DriftCreate();

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
        while (!ct.IsCancellationRequested)
        {
            var span = Transport.PeekData();
            if (span.IsEmpty) { Thread.Sleep(1); continue; }
            if (span.Length < sizeof(OecFrameHeader)) { Transport.ConsumeData(); continue; }
            fixed (byte* p = span)
            {
                var h = *(OecFrameHeader*)p;
                NativeMethods.DriftAdd(_drift, h.SampleIndex, h.HostQpcTicks);
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
                        SyncSubject.OnNext(new SyncPoint {
                            SampleIndex = h.SampleIndex,
                            HostQpcTicks = h.HostQpcTicks,
                            FpgaSampleRateHz = 30000.0
                        });
                        break;
                }
                if ((h.Flags & 0x0002) != 0) Interlocked.Increment(ref DropCount);
            }
            Transport.ConsumeData();
        }
    }

    private unsafe void DispatchBlock(in OecFrameHeader h, byte* p, int len, bool raw)
    {
        if (len < sizeof(OecFrameHeader) + sizeof(OecBlockSubheader)) return;
        var sh = *(OecBlockSubheader*)(p + sizeof(OecFrameHeader));
        var samples = new short[sh.NumChannels * sh.NumSamples];
        fixed (short* dst = samples)
        {
            Buffer.MemoryCopy(p + sizeof(OecFrameHeader) + sizeof(OecBlockSubheader),
                              dst, samples.Length * sizeof(short),
                              samples.Length * sizeof(short));
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

    public void Dispose()
    {
        _cts?.Cancel();
        _reader?.Join(500);
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
