using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reactive.Linq;
using System.Threading;
using Bonsai.OEconnect.Operators;
using Xunit;

namespace Bonsai.OEconnect.Tests;

/// <summary>
/// End-to-end: shells out to the gtest harness from the plugin (built by Phase 2)
/// to produce a live shmem session, then opens a Bonsai source operator against
/// the same endpoint and verifies the operator receives a RawBlock.
/// Skipped when the harness binary is not present.
/// </summary>
public class RoundTripTests
{
    [Fact]
    public void EndToEnd_PluginHarnessFeedsRawSamples()
    {
        var dir = AppContext.BaseDirectory;
        string? synth = null;
        var subdirs = new[] { "", "Debug", "Release", "RelWithDebInfo", "MinSizeRel" };
        for (int up = 0; up < 10 && dir != null; ++up, dir = Directory.GetParent(dir)?.FullName)
        {
            foreach (var sub in subdirs)
            {
                foreach (var name in new[] { "oec_plugin_synth", "oec_plugin_synth.exe" })
                {
                    var candidate = Path.Combine(dir!, "build-plugin", "Tests", sub, name);
                    if (File.Exists(candidate)) { synth = candidate; break; }
                }
                if (synth != null) break;
            }
            if (synth != null) break;
        }
        if (synth == null) return;     /* skip when not built */

        var endpoint = "/oeconnect.synth.bonsai-test.shm";
        var psi = new ProcessStartInfo(synth, $"\"{endpoint}\" 4 32") {
            RedirectStandardOutput = true, UseShellExecute = false
        };
        using var proc = Process.Start(psi)!;
        try
        {
            /* Give the producer a moment to create the shm region. */
            var firstLine = proc.StandardOutput.ReadLine();
            Assert.NotNull(firstLine);
            var op = new RawSamples { Endpoint = "shm://" + endpoint };
            var seen = 0;
            using var sub = op.Process().Subscribe(_ => Interlocked.Increment(ref seen));
            Thread.Sleep(1500);
            Assert.True(seen > 10, $"expected >10 RawBlock events, got {seen}");
        }
        finally
        {
            try { proc.Kill(); } catch { }
        }
    }
}
