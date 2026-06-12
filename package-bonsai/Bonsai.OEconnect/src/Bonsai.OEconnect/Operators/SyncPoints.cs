using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Raw (sample_index, host_qpc_ticks) pairs emitted ~1 Hz; useful for clients that roll their own clock.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class SyncPoints : SessionSource<SyncPoint>
{
    public IObservable<SyncPoint> Process() => Subscribe(s => s.SyncSubject);
}
