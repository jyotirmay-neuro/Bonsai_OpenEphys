using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Streams spike events (threshold crossings or sorter output) from OpenEphys. " +
    "NOT YET FUNCTIONAL: the plugin does not currently emit SPIKE frames, so this " +
    "node subscribes successfully but never produces a value. Detect spikes in " +
    "Bonsai from RawSamples until the plugin-side emitter lands. Waveform buffer " +
    "would be valid only inside OnNext.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class Spikes : SessionSource<SpikeEvent>
{
    public IObservable<SpikeEvent> Process() => Subscribe(s => s.SpikeSubject);
}
