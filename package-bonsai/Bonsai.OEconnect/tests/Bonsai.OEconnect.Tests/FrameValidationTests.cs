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

        public RawProducer(string name)
        {
            _name = name;
            var size = NativeMethods.RegionSize(SlotSize, SlotCount, CmdSlotSize, CmdSlotCount, AckSlotSize, AckSlotCount);
            Assert.Equal(OecStatus.Ok, NativeMethods.ShmCreate(_name, size, 1, out _shm, out _mapped, out _mappedSize));
            Assert.Equal(OecStatus.Ok, NativeMethods.RegionInit(_mapped, _mappedSize, SlotSize, SlotCount, CmdSlotSize, CmdSlotCount, AckSlotSize, AckSlotCount));
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
