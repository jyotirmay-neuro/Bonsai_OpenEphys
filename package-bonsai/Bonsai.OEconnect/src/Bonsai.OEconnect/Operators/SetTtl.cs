using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Asserts a TTL output line on the OE acquisition board via the OEconnect plugin.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class SetTtl
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("TTL output line number.")]
    public byte Line { get; set; }

    [Description("1 = high (rising edge), 0 = low (falling edge).")]
    public byte Edge { get; set; }

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Do(_x => CmdSender.SendSetTtl(session.Transport, Line, Edge != 0, out _))
                .Finally(() => SessionRegistry.Release(Endpoint, session));
        });
    }
}
