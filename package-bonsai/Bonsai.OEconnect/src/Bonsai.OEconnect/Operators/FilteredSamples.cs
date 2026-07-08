using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Streams the FILTERED_BLOCK stream from OpenEphys. The plugin does not filter " +
    "anything itself - it forwards whatever the upstream OE signal chain already " +
    "produced, so put a Bandpass Filter node BEFORE OEconnect for this to differ " +
    "from RawSamples. It is off by default: enable \"Stream filtered\" in the " +
    "plugin editor or this node never emits. Same int16 ADC-count format and " +
    "buffer-lifetime rules as RawSamples.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class FilteredSamples : SessionSource<RawBlock>
{
    public IObservable<RawBlock> Process() => Subscribe(s => s.FilteredSubject);
}
