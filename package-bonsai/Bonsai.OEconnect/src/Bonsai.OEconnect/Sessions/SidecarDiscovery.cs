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

    /// <summary>
    /// Returns the sidecar with the newest start time. The sidecar is only a
    /// pointer to a session's endpoints; its <c>started_unix_ns</c> is fixed at
    /// launch and must NOT be used as a liveness signal (a healthy session that
    /// has run for more than a few seconds would otherwise look "dead"). Actual
    /// liveness is verified after connecting, via the shared-region
    /// <c>producer_heartbeat_ns</c> the producer refreshes ~1 Hz.
    /// </summary>
    public static Sidecar? FindNewest(string dir)
    {
        Sidecar? newest = null;
        foreach (var f in Directory.EnumerateFiles(dir, "*.json"))
        {
            try
            {
                using var s = File.OpenRead(f);
                using var doc = JsonDocument.Parse(s);
                var root = doc.RootElement;
                var sc = new Sidecar(
                    root.GetProperty("pid").GetInt32(),
                    root.GetProperty("shm_region").GetString() ?? "",
                    root.GetProperty("zmq_fallback_endpoint").GetString() ?? "",
                    root.GetProperty("started_unix_ns").GetUInt64());
                if (newest is null || sc.StartedNs > newest.Value.StartedNs)
                    newest = sc;
            }
            catch { /* skip unparseable */ }
        }
        return newest;
    }
}
