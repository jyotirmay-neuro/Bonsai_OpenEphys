using System;
using System.IO;
using System.Text.Json;

namespace Bonsai.OEconnect.Sessions;

internal static class SidecarDiscovery
{
    public static string Dir()
    {
        var tmp = Path.GetTempPath();
        var dir = Path.Combine(tmp, "oeconnect", "sessions");
        Directory.CreateDirectory(dir);
        return dir;
    }

    public record struct Sidecar(int Pid, string Shm, string Zmq, ulong StartedNs);

    public static Sidecar? FindNewestLive(string dir, int maxAgeSeconds)
    {
        Sidecar? newest = null;
        var cutoffNs = (ulong)((DateTimeOffset.UtcNow.AddSeconds(-maxAgeSeconds)).ToUnixTimeMilliseconds() * 1_000_000);

        foreach (var f in Directory.EnumerateFiles(dir, "*.json"))
        {
            try
            {
                using var s = File.OpenRead(f);
                using var doc = JsonDocument.Parse(s);
                var root = doc.RootElement;
                var started = root.GetProperty("started_unix_ns").GetUInt64();
                if (started < cutoffNs) continue;
                var sc = new Sidecar(
                    root.GetProperty("pid").GetInt32(),
                    root.GetProperty("shm_region").GetString() ?? "",
                    root.GetProperty("zmq_fallback_endpoint").GetString() ?? "",
                    started);
                if (newest is null || sc.StartedNs > newest.Value.StartedNs)
                    newest = sc;
            }
            catch { /* skip unparseable */ }
        }
        return newest;
    }
}
