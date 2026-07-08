using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Data;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

/// <summary>
/// Adds <see cref="DateTimeOffset"/> per element using the session's drift fit.
/// </summary>
[Combinator]
[Description(
    "Pairs each block with the host wall-clock time of its first sample, corrected " +
    "for clock drift. A least-squares fit over the last 60 SYNC points maps FPGA " +
    "sample index to the plugin's high-resolution clock, which is then anchored to " +
    "UTC. Emits DateTimeOffset.MinValue until enough SYNC frames have arrived to " +
    "fit (about two seconds after the session starts).")]
[WorkflowElementCategory(ElementCategory.Transform)]
public class SampleToHostTime
{
    [Description(
        "Which OE session's drift fit to use. Empty = auto-discovery. Should " +
        "normally match the Endpoint of the source feeding this node.")]
    public string Endpoint { get; set; } = string.Empty;

    public IObservable<(RawBlock block, DateTimeOffset time)> Process(IObservable<RawBlock> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Select(b => (b, session.TrySampleToHostTime(b.SampleIndex, out var t)
                                 ? t
                                 : DateTimeOffset.MinValue))
                .Finally(() => SessionRegistry.Release(Endpoint, session));
        });
    }
}
