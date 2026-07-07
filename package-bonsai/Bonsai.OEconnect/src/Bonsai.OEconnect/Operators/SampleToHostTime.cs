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
[Description("Pairs each frame's sample_index with a drift-corrected DateTimeOffset.")]
[WorkflowElementCategory(ElementCategory.Transform)]
public class SampleToHostTime
{
    [Description("Endpoint of the session whose drift fit should be used; empty = auto-discovery.")]
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
