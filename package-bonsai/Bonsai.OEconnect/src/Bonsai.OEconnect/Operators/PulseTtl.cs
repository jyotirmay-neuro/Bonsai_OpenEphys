using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Pulses a TTL line high for WidthMicroseconds then back low. Implemented as one PULSE_TTL command -- width is enforced by the board adapter when supported.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class PulseTtl
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("TTL output line number.")]
    public byte Line { get; set; }

    [Description("Pulse width in microseconds.")]
    public uint WidthMicroseconds { get; set; } = 1000;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Do(_x => CmdSender.SendPulseTtl(session.Transport, Line, true, WidthMicroseconds, out _))
                .Finally(() => SessionRegistry.Release(Endpoint, session));
        });
    }
}
