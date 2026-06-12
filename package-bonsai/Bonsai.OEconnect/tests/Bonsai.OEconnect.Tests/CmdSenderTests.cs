using System;
using System.Runtime.InteropServices;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Sessions;
using Xunit;

namespace Bonsai.OEconnect.Tests;

public class CmdSenderTests
{
    [Fact]
    public void BuildCmd_SetTtl_HasExpectedLayout()
    {
        var frame = CmdSender.BuildCmd(OecCmds.SetTtl, 0xCAFE,
            new byte[] { 3, 1 });
        Assert.True(frame.Length >= Marshal.SizeOf<OecFrameHeader>() + 8);
        var h = MemoryMarshal.Read<OecFrameHeader>(frame);
        Assert.Equal(OecStreams.Cmd, h.StreamId);
        ushort cmdId = MemoryMarshal.Read<ushort>(frame.AsSpan(Marshal.SizeOf<OecFrameHeader>()));
        Assert.Equal(OecCmds.SetTtl, cmdId);
        uint cookie = MemoryMarshal.Read<uint>(frame.AsSpan(Marshal.SizeOf<OecFrameHeader>() + 2));
        Assert.Equal(0xCAFEu, cookie);
    }
}
