using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams continuous broadband samples from OpenEphys. Sample buffer is valid only inside OnNext -- clone before retaining.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class RawSamples : SessionSource<RawBlock>
{
    public IObservable<RawBlock> Process() => Subscribe(s => s.RawSubject);
}
