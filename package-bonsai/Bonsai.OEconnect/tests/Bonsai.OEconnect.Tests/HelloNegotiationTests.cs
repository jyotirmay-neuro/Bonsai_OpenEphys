using System;
using System.Runtime.InteropServices;
using System.Threading;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Sessions;
using Bonsai.OEconnect.Transport;
using Xunit;

namespace Bonsai.OEconnect.Tests;

/// <summary>
/// End-to-end HELLO handshake matrix (spec/oec-protocol-v1.md §8).
/// Spins up an in-process producer that owns a shmem region, optionally writes
/// a HELLO frame onto the data ring, then opens a consumer Session against
/// the same region and verifies the negotiated state.
/// </summary>
public class HelloNegotiationTests
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

    private static string ToEndpoint(string shmName) => "shm://" + shmName;

    private sealed class HelloProducer : IDisposable
    {
        private IntPtr _shm = IntPtr.Zero;
        private IntPtr _mapped = IntPtr.Zero;
        private UIntPtr _mappedSize = UIntPtr.Zero;
        private IntPtr _dataRing = IntPtr.Zero;
        private readonly string _shmName;

        public HelloProducer(string shmName) { _shmName = shmName; }

        public void Create()
        {
            var size = NativeMethods.RegionSize(SlotSize, SlotCount,
                CmdSlotSize, CmdSlotCount, AckSlotSize, AckSlotCount);
            Assert.NotEqual(UIntPtr.Zero, size);
            Assert.Equal(OecStatus.Ok,
                NativeMethods.ShmCreate(_shmName, size, 1, out _shm, out _mapped, out _mappedSize));
            Assert.Equal(OecStatus.Ok,
                NativeMethods.RegionInit(_mapped, _mappedSize, SlotSize, SlotCount,
                    CmdSlotSize, CmdSlotCount, AckSlotSize, AckSlotCount));
            Assert.Equal(OecStatus.Ok, NativeMethods.RingbufAttach(_mapped, 0, out _dataRing));
        }

        public unsafe void EmitHello(ushort major, ushort minor, uint pluginVer, uint libVer)
        {
            var slot = NativeMethods.RingbufAcquire(_dataRing, 0, out uint cap);
            Assert.NotEqual(IntPtr.Zero, slot);
            int headerSize = sizeof(OecFrameHeader);
            int bodySize   = sizeof(OecHelloBody);
            Assert.True(cap >= headerSize + bodySize);
            var hdr = new OecFrameHeader();
            NativeMethods.FrameInit(ref hdr, OecStreams.Hello, (uint)bodySize, 0, 0, 0);
            *(OecFrameHeader*)slot = hdr;
            var body = new OecHelloBody
            {
                ProtocolMajor = major, ProtocolMinor = minor,
                PluginVersion = pluginVer, LibVersion = libVer, Reserved = 0
            };
            *(OecHelloBody*)IntPtr.Add(slot, headerSize) = body;
            NativeMethods.RingbufPublish(_dataRing);
        }

        public void Dispose()
        {
            if (_dataRing != IntPtr.Zero) { NativeMethods.RingbufDetach(_dataRing); _dataRing = IntPtr.Zero; }
            if (_shm      != IntPtr.Zero) { NativeMethods.ShmClose(_shm);            _shm      = IntPtr.Zero; }
            NativeMethods.ShmUnlink(_shmName);
        }
    }

    private static Session OpenConsumer(string endpoint)
    {
        var client = new ShmemClient();
        Assert.True(client.Start(endpoint));
        var session = new Session(client, endpoint);
        session.StartReader();
        return session;
    }

    [Fact]
    public void IdenticalV1_1_NegotiatesSilently()
    {
        var name = MakeShmName("identical");
        using var prod = new HelloProducer(name);
        prod.Create();
        prod.EmitHello(1, 1, 0x01010000u, 0x01010000u);

        using var session = OpenConsumer(ToEndpoint(name));
        SpinUntil(() => session.Negotiated, TimeSpan.FromSeconds(1));

        Assert.True(session.Negotiated);
        Assert.Equal(1, session.RemoteMajor);
        Assert.Equal(1, session.RemoteMinor);
        Assert.Equal(0x01010000u, session.RemotePluginVer);
        Assert.Equal(0x01010000u, session.RemoteLibVer);
    }

    [Fact]
    public void RemoteNewerMinor_Accepted()
    {
        var name = MakeShmName("newer");
        using var prod = new HelloProducer(name);
        prod.Create();
        prod.EmitHello(1, 2, 0x01020000u, 0x01020000u);

        using var session = OpenConsumer(ToEndpoint(name));
        SpinUntil(() => session.Negotiated, TimeSpan.FromSeconds(1));

        Assert.True(session.Negotiated);
        Assert.Equal(1, session.RemoteMajor);
        Assert.Equal(2, session.RemoteMinor);
    }

    [Fact]
    public void NoHelloWithin2s_FallsBackToV1_0()
    {
        var name = MakeShmName("v10");
        using var prod = new HelloProducer(name);
        prod.Create();
        /* Producer intentionally never emits HELLO -- mimics a v1.0 plugin. */

        using var session = OpenConsumer(ToEndpoint(name));
        Thread.Sleep(2500);

        Assert.False(session.Negotiated);
        Assert.Equal(1, session.RemoteMajor);
        Assert.Equal(0, session.RemoteMinor);
    }

    [Fact]
    public void MajorMismatch_RaisesOnError()
    {
        var name = MakeShmName("major");
        using var prod = new HelloProducer(name);
        prod.Create();
        prod.EmitHello(2, 0, 0x02000000u, 0x02000000u);

        using var session = OpenConsumer(ToEndpoint(name));
        Exception? captured = null;
        using var _ = session.RawSubject.Subscribe(_ => { }, ex => captured = ex);
        SpinUntil(() => captured != null, TimeSpan.FromSeconds(1));

        Assert.NotNull(captured);
        Assert.IsType<OpenEphysConnectionException>(captured);
        Assert.Contains("v2", captured!.Message);
    }

    private static void SpinUntil(Func<bool> cond, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (!cond() && DateTime.UtcNow < deadline) Thread.Sleep(10);
    }
}
