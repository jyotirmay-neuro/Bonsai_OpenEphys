using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Streams spike events flowing through the OEconnect node in the OpenEphys " +
    "signal chain. OEconnect does not detect spikes itself - place a Spike " +
    "Detector or sorter UPSTREAM of it, or this node never fires. Waveforms are " +
    "int16 ADC counts (multiply by the channel's bitVolts for microvolts), " +
    "channel-major, and ElectrodeId is the spike channel's global index. Requires " +
    "\"Stream spikes\" enabled in the plugin editor (on by default).")]
[WorkflowElementCategory(ElementCategory.Source)]
public class Spikes : SessionSource<SpikeEvent>
{
    public IObservable<SpikeEvent> Process() => Subscribe(s => s.SpikeSubject);
}
