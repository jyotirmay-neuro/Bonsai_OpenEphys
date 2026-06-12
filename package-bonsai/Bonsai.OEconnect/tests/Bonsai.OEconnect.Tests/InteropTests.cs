using Bonsai.OEconnect.Interop;
using Xunit;

namespace Bonsai.OEconnect.Tests;

public class InteropTests
{
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
}
