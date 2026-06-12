using System;
using System.Runtime.InteropServices;
using System.Threading;
using Bonsai.OEconnect.Interop;
using Bonsai.OEconnect.Transport;

namespace Bonsai.OEconnect.Sessions;

internal static class CmdSender
{
    private static int _cookieCounter = 1;

    public static uint NextCookie() => (uint)Interlocked.Increment(ref _cookieCounter);

    /// <summary>Builds a CMD frame: header + {cmd_id, cookie, body...}.</summary>
    public static byte[] BuildCmd(ushort cmdId, uint cookie, ReadOnlySpan<byte> body)
    {
        int hdr = Marshal.SizeOf<OecFrameHeader>();
        int payload = 6 + body.Length;          /* cmd_id_u16 + cookie_u32 + body */
        var buf = new byte[hdr + payload];
        unsafe
        {
            fixed (byte* p = buf)
            {
                OecFrameHeader* fh = (OecFrameHeader*)p;
                NativeMethods.FrameInit(ref *fh, OecStreams.Cmd, (uint)payload, 0, 0, 0);
                byte* b = p + hdr;
                *(ushort*)(b + 0) = cmdId;
                *(uint*)  (b + 2) = cookie;
                body.CopyTo(new Span<byte>(b + 6, body.Length));
            }
        }
        return buf;
    }

    public static bool SendSetTtl(ITransportClient t, byte line, bool high, out uint cookie)
    {
        cookie = NextCookie();
        Span<byte> body = stackalloc byte[2] { line, (byte)(high ? 1 : 0) };
        var frame = BuildCmd(OecCmds.SetTtl, cookie, body);
        return t.PostCmd(frame);
    }

    public static bool SendPulseTtl(ITransportClient t, byte line, bool high, uint widthMicros, out uint cookie)
    {
        cookie = NextCookie();
        Span<byte> body = stackalloc byte[6];
        body[0] = line;
        body[1] = (byte)(high ? 1 : 0);
        /* Little-endian uint32 width — written byte-by-byte for cross-target safety. */
        body[2] = (byte)(widthMicros & 0xFF);
        body[3] = (byte)((widthMicros >> 8) & 0xFF);
        body[4] = (byte)((widthMicros >> 16) & 0xFF);
        body[5] = (byte)((widthMicros >> 24) & 0xFF);
        var frame = BuildCmd(OecCmds.PulseTtl, cookie, body);
        return t.PostCmd(frame);
    }

    public static bool SendStartRecord(ITransportClient t, string directory, string prefix, out uint cookie)
    {
        cookie = NextCookie();
        var dirBytes = System.Text.Encoding.UTF8.GetBytes(directory);
        var preBytes = System.Text.Encoding.UTF8.GetBytes(prefix);
        var body = new byte[2 + dirBytes.Length + 2 + preBytes.Length];
        ushort dlen = (ushort)dirBytes.Length;
        body[0] = (byte)(dlen & 0xFF);
        body[1] = (byte)(dlen >> 8);
        Buffer.BlockCopy(dirBytes, 0, body, 2, dirBytes.Length);
        ushort plen = (ushort)preBytes.Length;
        body[2 + dirBytes.Length]     = (byte)(plen & 0xFF);
        body[3 + dirBytes.Length]     = (byte)(plen >> 8);
        Buffer.BlockCopy(preBytes, 0, body, 4 + dirBytes.Length, preBytes.Length);
        return t.PostCmd(BuildCmd(OecCmds.StartRecord, cookie, body));
    }

    public static bool SendStopRecord(ITransportClient t, out uint cookie)
    {
        cookie = NextCookie();
        return t.PostCmd(BuildCmd(OecCmds.StopRecord, cookie, ReadOnlySpan<byte>.Empty));
    }
}
