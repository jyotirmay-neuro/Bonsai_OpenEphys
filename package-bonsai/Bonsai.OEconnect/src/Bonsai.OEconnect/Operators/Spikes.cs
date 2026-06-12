using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams spike events (threshold or sorter output) from OpenEphys. Waveform buffer valid only inside OnNext.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class Spikes : SessionSource<SpikeEvent>
{
    public IObservable<SpikeEvent> Process() => Subscribe(s => s.SpikeSubject);
}
