using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Tells OpenEphys to stop recording, once per element received, and passes the " +
    "element through unchanged. Like StartRecording this is a \"slow\" command, " +
    "acknowledged immediately and executed on a worker thread. Stops every Record " +
    "Node in the OE signal chain; acquisition itself keeps running.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class StopRecording
{
    [Description(
        "Which OE session to command. Empty = auto-discovery (newest live " +
        "plugin). Otherwise \"shm://...\" or \"tcp://host:5557|tcp://host:5558\".")]
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
