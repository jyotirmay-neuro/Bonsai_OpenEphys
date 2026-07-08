using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using Bonsai.OEconnect.Interop;
using Xunit;

namespace Bonsai.OEconnect.Tests;

public class InteropTests
{
    static readonly IReadOnlyDictionary<string, ushort> GoldenStreams = new Dictionary<string, ushort>
    {
        ["raw_block.bin"]   = OecStreams.RawBlock,
        ["ttl_event.bin"]   = OecStreams.TtlEvent,
        ["spike.bin"]       = OecStreams.Spike,
        ["sync.bin"]        = OecStreams.Sync,
        ["cmd_set_ttl.bin"] = OecStreams.Cmd,
        ["ack_ok.bin"]      = OecStreams.Ack,
        ["hello.bin"]       = OecStreams.Hello,
    };

    static string? FindCorpusDir()
    {
        for (var d = new DirectoryInfo(System.AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            var cand = Path.Combine(d.FullName, "tests", "golden", "v1.0");
            if (File.Exists(Path.Combine(cand, "hello.bin"))) return cand;
        }
        return null;
    }

    [Fact]
    public void GoldenCorpus_EveryFrameDecodesAndValidates()
    {
        // Cross-runtime guard: the same tests/golden/v1.0 frames are decoded by
        // the gtest side (GoldenCorpus.EveryFrameDecodesAndValidates). A drift in
        // either runtime's parsing is caught here.
        var dir = FindCorpusDir();
        Assert.NotNull(dir);

        int headerSize = Marshal.SizeOf<OecFrameHeader>();
        foreach (var kv in GoldenStreams)
        {
            var path = Path.Combine(dir, kv.Key);
            Assert.True(File.Exists(path), $"missing golden frame: {kv.Key}");

            var bytes = File.ReadAllBytes(path);
            Assert.True(bytes.Length >= headerSize, kv.Key);

            var h = MemoryMarshal.Read<OecFrameHeader>(bytes);
            Assert.Equal(OecStatus.Ok, NativeMethods.FrameValidate(in h));
            Assert.Equal(kv.Value, h.StreamId);
            Assert.Equal((uint)(bytes.Length - headerSize), h.PayloadLen);
        }
    }
    [Fact]
    public void FrameInit_PopulatesMagicAndVersion()
    {
        var h = new OecFrameHeader();
        NativeMethods.FrameInit(ref h, OecStreams.RawBlock, 1234, 9, 10, 0);
        Assert.Equal(0x3143454Fu, h.Magic);
        Assert.Equal(OecStreams.RawBlock, h.StreamId);
        Assert.Equal(1234u, h.PayloadLen);
        Assert.Equal(9ul, h.SampleIndex);
    }

    [Fact]
    public void FrameValidate_AcceptsWellFormed()
    {
        var h = new OecFrameHeader();
        NativeMethods.FrameInit(ref h, OecStreams.TtlEvent, 4, 0, 0, 0);
        Assert.Equal(OecStatus.Ok, NativeMethods.FrameValidate(in h));
    }

    [Fact]
    public void DriftFit_RecoversLinear()
    {
        var f = NativeMethods.DriftCreate();
        try
        {
            for (ulong s = 0; s < 50; ++s) NativeMethods.DriftAdd(f, s, 100 + 33 * s);
            Assert.Equal(OecStatus.Ok, NativeMethods.DriftFit(f, out var a, out var b));
            Assert.Equal(33.0, a, 9);
            Assert.Equal(100.0, b, 6);
        }
        finally { NativeMethods.DriftDestroy(f); }
    }

    /// <summary>
    /// Spec §2.5: divergence is detected via residual RMS — ~0 for a perfect line,
    /// large once a clock step breaks the sample↔clock relationship.
    /// </summary>
    [Fact]
    public void DriftResidualRms_IsZeroForPerfectLineAndLargeAfterAJump()
    {
        var f = NativeMethods.DriftCreate();
        try
        {
            for (ulong s = 0; s < 50; ++s) NativeMethods.DriftAdd(f, s, 100 + 33 * s);
            Assert.Equal(OecStatus.Ok, NativeMethods.DriftFit(f, out _, out _));
            Assert.InRange(NativeMethods.DriftResidualRms(f), 0.0, 1e-6);

            for (ulong s = 50; s < 60; ++s) NativeMethods.DriftAdd(f, s, 100 + 33 * s + 100_000);
            Assert.Equal(OecStatus.Ok, NativeMethods.DriftFit(f, out _, out _));
            Assert.True(NativeMethods.DriftResidualRms(f) > 1000.0,
                "a large clock step must show up as a large residual");
        }
        finally { NativeMethods.DriftDestroy(f); }
    }

    /// <summary>Reset clears the fit, so predictions fall back to 0 until refit.</summary>
    [Fact]
    public void DriftReset_ClearsTheFit()
    {
        var f = NativeMethods.DriftCreate();
        try
        {
            for (ulong s = 0; s < 50; ++s) NativeMethods.DriftAdd(f, s, 100 + 33 * s);
            Assert.Equal(OecStatus.Ok, NativeMethods.DriftFit(f, out _, out _));
            Assert.NotEqual(0ul, NativeMethods.DriftPredictQpc(f, 10));
            Assert.True(NativeMethods.DriftResidualRms(f) >= 0.0);

            NativeMethods.DriftReset(f);

            Assert.Equal(0ul, NativeMethods.DriftPredictQpc(f, 10));
            Assert.Equal(-1.0, NativeMethods.DriftResidualRms(f));
            Assert.Equal(OecStatus.EParse, NativeMethods.DriftFit(f, out _, out _));
        }
        finally { NativeMethods.DriftDestroy(f); }
    }
}
