using System;
using System.Threading;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Sessions;
using Bonsai.OEconnect.Transport;
using Xunit;

namespace Bonsai.OEconnect.Tests;

/// <summary>
/// Spec §2.6: a consumer must check magic and version_major before trusting any
/// field of a frame. Previously nothing on either side called oec_frame_validate().
/// </summary>
public class FrameValidationTests
{
    private const uint SlotSize = 65536u, SlotCount = 256u;
    private const uint CmdSlotSize = 4096u, CmdSlotCount = 64u;
    private const uint AckSlotSize = 4096u, AckSlotCount = 64u;

    private static string MakeShmName(string tag)
    {
        var unique = $"{tag}.{Environment.ProcessId}.{Guid.NewGuid():N}";
        return OperatingSystem.IsWindows()
            ? $"Local\\oeconnect.test.{unique}"
            : $"/oeconnect.test.{unique}";
    }

    /// <summary>Owns a region and writes arbitrary frames onto its data ring.</summary>
    private sealed class RawProducer : IDisposable
    {
        private IntPtr _shm, _mapped, _dataRing;
        private UIntPtr _mappedSize;
        private readonly string _name;

        public RawProducer(string name, uint slotSize = SlotSize, uint slotCount = SlotCount)
        {
            _name = name;
            var size = NativeMethods.RegionSize(slotSize, slotCount, CmdSlotSize, CmdSlotCount, AckSlotSize, AckSlotCount);
            Assert.NotEqual(UIntPtr.Zero, size);
            Assert.Equal(OecStatus.Ok, NativeMethods.ShmCreate(_name, size, 1, out _shm, out _mapped, out _mappedSize));
            Assert.Equal(OecStatus.Ok, NativeMethods.RegionInit(_mapped, _mappedSize, slotSize, slotCount, CmdSlotSize, CmdSlotCount, AckSlotSize, AckSlotCount));
            Assert.Equal(OecStatus.Ok, NativeMethods.RingbufAttach(_mapped, 0, out _dataRing));
        }

        /// <summary>Publishes a header with optional mutation, plus a zeroed payload.</summary>
        public unsafe void Emit(ushort streamId, uint payloadLen, Action<IntPtr>? mutate = null)
        {
            var slot = NativeMethods.RingbufAcquire(_dataRing, 0, out _);
            Assert.NotEqual(IntPtr.Zero, slot);
            var hdr = new OecFrameHeader();
            NativeMethods.FrameInit(ref hdr, streamId, payloadLen, 0, 0, 0);
            *(OecFrameHeader*)slot = hdr;
            new Span<byte>((byte*)slot + sizeof(OecFrameHeader), (int)payloadLen).Clear();
            mutate?.Invoke(slot);
            NativeMethods.RingbufPublish(_dataRing);
        }

        /// <summary>Publishes one frame split across `slots` consecutive ring slots.</summary>
        public unsafe void EmitSpan(byte[] frame, uint slotSize, ulong slots)
        {
            long copied = 0;
            for (ulong i = 0; i < slots; ++i)
            {
                var slot = NativeMethods.RingbufAcquire(_dataRing, 0, out _);
                Assert.NotEqual(IntPtr.Zero, slot);
                long chunk = Math.Min(frame.Length - copied, slotSize);
                frame.AsSpan((int)copied, (int)chunk)
                     .CopyTo(new Span<byte>(slot.ToPointer(), (int)chunk));
                copied += chunk;
                NativeMethods.RingbufPublish(_dataRing);
            }
            Assert.Equal(frame.Length, copied);
        }

        public void Dispose()
        {
            if (_dataRing != IntPtr.Zero) NativeMethods.RingbufDetach(_dataRing);
            if (_shm != IntPtr.Zero) NativeMethods.ShmClose(_shm);
            NativeMethods.ShmUnlink(_name);
        }
    }

    /// <summary>
    /// Attaches a consumer WITHOUT starting its reader. Subjects are hot, so a
    /// subscriber that arrives after the reader has already drained the ring misses
    /// the notification entirely — subscribe first, then call StartReader().
    /// </summary>
    private static Session AttachConsumer(string name)
    {
        var client = new ShmemClient();
        Assert.True(client.Start("shm://" + name));
        return new Session(client, "shm://" + name);
    }

    private static void SpinUntil(Func<bool> cond, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (!cond() && DateTime.UtcNow < deadline) Thread.Sleep(10);
    }

    [Fact]
    public void FrameValidateAcceptsAWellFormedHeader()
    {
        var hdr = new OecFrameHeader();
        NativeMethods.FrameInit(ref hdr, OecStreams.RawBlock, 0, 0, 0, 0);
        Assert.Equal(OecStatus.Ok, NativeMethods.FrameValidate(in hdr));
    }

    [Fact]
    public void FrameValidateRejectsBadMagic()
    {
        var hdr = new OecFrameHeader();
        NativeMethods.FrameInit(ref hdr, OecStreams.RawBlock, 0, 0, 0, 0);
        hdr.Magic = 0xDEADBEEF;
        Assert.Equal(OecStatus.EBadMagic, NativeMethods.FrameValidate(in hdr));
    }

    [Fact]
    public void FrameValidateRejectsMajorMismatchButAcceptsMinor()
    {
        var hdr = new OecFrameHeader();
        NativeMethods.FrameInit(ref hdr, OecStreams.RawBlock, 0, 0, 0, 0);

        hdr.VersionMajor = (byte)(OecProtocol.VersionMajor + 1);
        Assert.Equal(OecStatus.EVersionMismatch, NativeMethods.FrameValidate(in hdr));

        hdr.VersionMajor = OecProtocol.VersionMajor;
        hdr.VersionMinor = (byte)(OecProtocol.VersionMinor + 7);   // forward-compatible
        Assert.Equal(OecStatus.Ok, NativeMethods.FrameValidate(in hdr));
    }

    [Fact]
    public unsafe void ReaderDropsCorruptFramesWithoutCountingThemAsData()
    {
        var name = MakeShmName("badmagic");
        using var prod = new RawProducer(name);

        /* Three frames whose magic is corrupt. A RAW_BLOCK subheader claiming a
         * huge geometry would previously have been parsed. */
        for (int i = 0; i < 3; ++i)
            prod.Emit(OecStreams.RawBlock, 64, slot => ((OecFrameHeader*)slot)->Magic = 0xDEADBEEF);

        using var session = AttachConsumer(name);
        var blocks = 0;
        using var sub = session.RawSubject.Subscribe(_ => Interlocked.Increment(ref blocks), _ => { });
        session.StartReader();

        SpinUntil(() => Interlocked.Read(ref session.InvalidFrameCount) >= 3, TimeSpan.FromSeconds(2));

        Assert.Equal(3, Interlocked.Read(ref session.InvalidFrameCount));
        Assert.Equal(0, Interlocked.Read(ref session.FrameCount));
        Assert.Equal(0, blocks);
    }

    [Fact]
    public unsafe void ReaderRaisesProtocolVersionMismatchOnMajorMismatch()
    {
        var name = MakeShmName("badmajor");
        using var prod = new RawProducer(name);
        prod.Emit(OecStreams.RawBlock, 64,
            slot => ((OecFrameHeader*)slot)->VersionMajor = (byte)(OecProtocol.VersionMajor + 1));

        using var session = AttachConsumer(name);
        Exception? captured = null;
        using var sub = session.RawSubject.Subscribe(_ => { }, ex => captured = ex);
        session.StartReader();

        SpinUntil(() => captured != null, TimeSpan.FromSeconds(2));

        Assert.NotNull(captured);
        Assert.IsType<OpenEphysConnectionException>(captured);
        Assert.Contains("PROTOCOL_VERSION_MISMATCH", captured!.Message);
        Assert.Equal(0, Interlocked.Read(ref session.FrameCount));
    }

    /// <summary>
    /// Spec §4.7: the producer's slot geometry is editor-configurable, so the
    /// consumer must take it from the region header rather than assume the
    /// defaults. A 256 KiB × 64 region carries a block far larger than 64 KiB.
    /// </summary>
    [Fact]
    public unsafe void ConsumerReadsNonDefaultRingGeometryFromTheHeader()
    {
        const uint bigSlot = 262144u, fewSlots = 64u;
        const int channels = 1024, samples = 64;

        var name = MakeShmName("geometry");
        using var prod = new RawProducer(name, bigSlot, fewSlots);

        uint payload = (uint)(sizeof(OecBlockSubheader) + channels * samples * sizeof(short));
        Assert.True(payload > 65536, "payload must exceed the default slot size");

        prod.Emit(OecStreams.RawBlock, payload, slot =>
        {
            var sh = (OecBlockSubheader*)((byte*)slot + sizeof(OecFrameHeader));
            sh->NumChannels = channels;
            sh->NumSamples = samples;
            sh->Dtype = 0;
        });

        using var session = AttachConsumer(name);
        RawBlock? got = null;
        using var sub = session.RawSubject.Subscribe(b => got ??= b.Clone(), _ => { });
        session.StartReader();

        SpinUntil(() => got != null, TimeSpan.FromSeconds(2));

        Assert.NotNull(got);
        Assert.Equal(channels, got!.Value.NumChannels);
        Assert.Equal(samples, got.Value.NumSamples);
        Assert.Equal(channels * samples, got.Value.Samples.Length);
    }

    /// <summary>
    /// Spec §3.1: SYNC carries {qpc_freq, sample_rate} followed by one packed
    /// 11-byte stream_meta per stream. Count is derived from payload_len, so an old
    /// producer advertising none must still parse.
    /// </summary>
    [Fact]
    public unsafe void SyncFrameStreamMetaIsParsed()
    {
        var name = MakeShmName("syncmeta");
        using var prod = new RawProducer(name);

        const int metaSize = 11;
        prod.Emit(OecStreams.Sync, (uint)(16 + 2 * metaSize), slot =>
        {
            byte* body = (byte*)slot + sizeof(OecFrameHeader);
            *(ulong*)(body + 0) = 10_000_000ul;      // qpc_freq_hz
            *(double*)(body + 8) = 30000.0;          // fpga_sample_rate_hz

            byte* m0 = body + 16;
            m0[0] = 0;
            *(ushort*)(m0 + 1) = 384;
            *(double*)(m0 + 3) = 30000.0;

            byte* m1 = m0 + metaSize;
            m1[0] = 1;
            *(ushort*)(m1 + 1) = 384;
            *(double*)(m1 + 3) = 2500.0;
        });

        using var session = AttachConsumer(name);
        SyncPoint? sync = null;
        using var sub = session.SyncSubject.Subscribe(s => sync ??= s, _ => { });
        session.StartReader();

        SpinUntil(() => sync != null, TimeSpan.FromSeconds(2));

        Assert.NotNull(sync);
        Assert.Equal(30000.0, sync!.Value.FpgaSampleRateHz);

        var streams = sync.Value.Streams.Span;
        Assert.Equal(2, streams.Length);
        Assert.Equal(0, streams[0].SourceId);
        Assert.Equal(384, streams[0].NumChannels);
        Assert.Equal(30000.0, streams[0].SampleRateHz);
        Assert.Equal(1, streams[1].SourceId);
        Assert.Equal(2500.0, streams[1].SampleRateHz);

        /* Also cached on the session for lookup by source_id. */
        Assert.Equal(2, session.Streams.Length);
    }

    /// <summary>A producer that advertises no streams (16-byte SYNC) still parses.</summary>
    [Fact]
    public unsafe void SyncFrameWithoutStreamMetaStillParses()
    {
        var name = MakeShmName("syncbare");
        using var prod = new RawProducer(name);
        prod.Emit(OecStreams.Sync, 16, slot =>
        {
            byte* body = (byte*)slot + sizeof(OecFrameHeader);
            *(ulong*)(body + 0) = 10_000_000ul;
            *(double*)(body + 8) = 25000.0;
        });

        using var session = AttachConsumer(name);
        SyncPoint? sync = null;
        using var sub = session.SyncSubject.Subscribe(s => sync ??= s, _ => { });
        session.StartReader();

        SpinUntil(() => sync != null, TimeSpan.FromSeconds(2));

        Assert.NotNull(sync);
        Assert.Equal(25000.0, sync!.Value.FpgaSampleRateHz);
        Assert.Equal(0, sync.Value.Streams.Length);
    }

    /// <summary>Blocks carry the emitting stream's source_id (spec §3.2).</summary>
    [Fact]
    public unsafe void BlockCarriesItsSourceId()
    {
        var name = MakeShmName("srcid");
        using var prod = new RawProducer(name);

        const int channels = 2, samples = 4;
        uint payload = (uint)(sizeof(OecBlockSubheader) + channels * samples * sizeof(short));
        prod.Emit(OecStreams.RawBlock, payload, slot =>
        {
            var sh = (OecBlockSubheader*)((byte*)slot + sizeof(OecFrameHeader));
            sh->NumChannels = channels;
            sh->NumSamples = samples;
            sh->SourceId = 3;
        });

        using var session = AttachConsumer(name);
        RawBlock? got = null;
        using var sub = session.RawSubject.Subscribe(b => got ??= b.Clone(), _ => { });
        session.StartReader();

        SpinUntil(() => got != null, TimeSpan.FromSeconds(2));

        Assert.NotNull(got);
        Assert.Equal(3, got!.Value.SourceId);
    }

    /// <summary>
    /// Spec §4.3: a frame larger than one slot spans consecutive slots with
    /// BIT_CONTINUATION. The consumer must stitch them back into one frame, and must
    /// not parse a span until every slot has been published.
    /// </summary>
    [Fact]
    public unsafe void ContinuationSpanIsReassembledIntoOneFrame()
    {
        const uint slotSize = 65536u, slotCount = 64u;
        const int channels = 1024, samples = 64;   // 128 KiB payload -> 3 slots

        var name = MakeShmName("continue");
        using var prod = new RawProducer(name, slotSize, slotCount);

        int payload = sizeof(OecBlockSubheader) + channels * samples * sizeof(short);
        long total = sizeof(OecFrameHeader) + payload;
        ulong slots = (ulong)((total + slotSize - 1) / slotSize);
        Assert.Equal(3ul, slots);

        /* Build the whole frame, then hand it out slot by slot, exactly as
         * ShmemTransport::publishLargeFrame does. */
        var frame = new byte[total];
        fixed (byte* f = frame)
        {
            var hdr = new OecFrameHeader();
            NativeMethods.FrameInit(ref hdr, OecStreams.RawBlock, (uint)payload, 7, 0,
                                    OecFlags.Continuation);
            *(OecFrameHeader*)f = hdr;

            var sh = (OecBlockSubheader*)(f + sizeof(OecFrameHeader));
            sh->NumChannels = channels;
            sh->NumSamples = samples;
            sh->SourceId = 1;

            var samplesPtr = (short*)(f + sizeof(OecFrameHeader) + sizeof(OecBlockSubheader));
            for (int i = 0; i < channels * samples; ++i) samplesPtr[i] = (short)(i & 0x7FFF);
        }

        prod.EmitSpan(frame, slotSize, slots);

        using var session = AttachConsumer(name);
        RawBlock? got = null;
        using var sub = session.RawSubject.Subscribe(b => got ??= b.Clone(), _ => { });
        session.StartReader();

        SpinUntil(() => got != null, TimeSpan.FromSeconds(3));

        Assert.NotNull(got);
        Assert.Equal(channels, got!.Value.NumChannels);
        Assert.Equal(samples, got.Value.NumSamples);
        Assert.Equal(1, got.Value.SourceId);
        Assert.Equal(7ul, got.Value.SampleIndex);

        var s = got.Value.Samples.Span;
        Assert.Equal(channels * samples, s.Length);
        for (int i = 0; i < s.Length; ++i)
            Assert.Equal((short)(i & 0x7FFF), s[i]);

        /* Exactly one frame: the span consumed all three slots, leaving nothing. */
        Assert.Equal(1, Interlocked.Read(ref session.FrameCount));
    }

    [Fact]
    public unsafe void ErrorFrameSurfacesCodeAndMessage()
    {
        var name = MakeShmName("errframe");
        using var prod = new RawProducer(name);

        /* ERROR body: {code_u16, utf8_len_u16, utf8_msg[]} */
        const string msg = "board SDK too old";
        var text = System.Text.Encoding.UTF8.GetBytes(msg);
        prod.Emit(OecStreams.Error, (uint)(4 + text.Length), slot =>
        {
            byte* body = (byte*)slot + sizeof(OecFrameHeader);
            *(ushort*)(body + 0) = OecErrors.UnsupportedBoardSdk;
            *(ushort*)(body + 2) = (ushort)text.Length;
            text.AsSpan().CopyTo(new Span<byte>(body + 4, text.Length));
        });

        using var session = AttachConsumer(name);
        SessionStatus? status = null;
        using var sub = session.StatusSubject.Subscribe(s => status = s, _ => { });
        session.StartReader();

        SpinUntil(() => status != null, TimeSpan.FromSeconds(2));

        Assert.NotNull(status);
        Assert.Equal(OecErrors.UnsupportedBoardSdk, status!.LastErrorCode);
        Assert.Equal(msg, status.LastErrorMessage);
    }
}
