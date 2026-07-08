using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Streams TTL edges flowing through the OEconnect node on OpenEphys' event bus " +
    "- board digital inputs and any upstream event generator. Each event carries " +
    "the line, the edge direction, and the FPGA sample index it occurred at. " +
    "Requires \"Stream TTL events\" enabled in the plugin editor (on by default). " +
    "Note: edges that OEconnect itself issues via SetTtl/PulseTtl are published " +
    "downstream and recorded, but are not echoed back through this node.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class TtlEvents : SessionSource<TtlEvent>
{
    public IObservable<TtlEvent> Process() => Subscribe(s => s.TtlSubject);
}
