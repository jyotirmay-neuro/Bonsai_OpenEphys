using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams filtered (LFP / spike-band) continuous samples from OpenEphys. Sample buffer is valid only inside OnNext.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class FilteredSamples : SessionSource<RawBlock>
{
    public IObservable<RawBlock> Process() => Subscribe(s => s.FilteredSubject);
}
