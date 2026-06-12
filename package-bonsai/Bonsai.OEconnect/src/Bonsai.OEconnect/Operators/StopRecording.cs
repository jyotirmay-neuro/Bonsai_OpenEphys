using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Sends STOP_RECORD to OpenEphys.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class StopRecording
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Do(_x => CmdSender.SendStopRecord(session.Transport, out _))
                .Finally(() => SessionRegistry.Release(Endpoint, session));
        });
    }
}
