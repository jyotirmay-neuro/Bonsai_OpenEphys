using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Streams TTL edges from OpenEphys' event bus (includes Bonsai-issued TTLs echoed back).")]
[WorkflowElementCategory(ElementCategory.Source)]
public class TtlEvents : SessionSource<TtlEvent>
{
    public IObservable<TtlEvent> Process() => Subscribe(s => s.TtlSubject);
}
