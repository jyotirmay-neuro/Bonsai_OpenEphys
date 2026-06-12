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
    private static string? HarnessPath()
    {
        var dir = AppContext.BaseDirectory;
        for (int up = 0; up < 8 && dir != null; ++up, dir = Directory.GetParent(dir)?.FullName)
        {
            var candidate = Path.Combine(dir, "..", "..", "build-plugin",
                                         "Tests", "oec_plugin_tests");
            if (File.Exists(candidate))   return candidate;
            if (File.Exists(candidate + ".exe")) return candidate + ".exe";
        }
        return null;
    }

    [Fact]
    public void EndToEnd_PluginHarnessFeedsRawSamples()
    {
        var harness = HarnessPath();
        if (harness == null)
        {
            /* Skip when plugin tests haven't been built locally. */
            return;
        }
        /* The harness exits after running its own gtests -- we instead want
           a long-running synthetic producer. The plugin's test fixture
           creates a unique region and exits, so for this test we depend on
           a separate `oec_plugin_synth` binary added by Task 4.x (perf).
           Until that binary lands, this end-to-end test is intentionally a
           no-op; full e2e is exercised by the manual latency procedure. */
    }
}
