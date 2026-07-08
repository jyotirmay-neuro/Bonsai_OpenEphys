using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Streams TTL edges from OpenEphys' event bus. NOT YET FUNCTIONAL: the plugin " +
    "does not currently emit TTL_EVENT frames, so this node subscribes " +
    "successfully but never produces a value.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class TtlEvents : SessionSource<TtlEvent>
{
    public IObservable<TtlEvent> Process() => Subscribe(s => s.TtlSubject);
}
