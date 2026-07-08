using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Emits one SyncPoint per second carrying the FPGA sample index paired with " +
    "the plugin host's high-resolution clock reading, plus the clock frequency " +
    "and acquisition sample rate. Use this to roll your own clock alignment; " +
    "SampleToHostTime already does it for you. Also a convenient liveness probe: " +
    "if these stop arriving, the plugin is no longer publishing.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class SyncPoints : SessionSource<SyncPoint>
{
    public IObservable<SyncPoint> Process() => Subscribe(s => s.SyncSubject);
}
