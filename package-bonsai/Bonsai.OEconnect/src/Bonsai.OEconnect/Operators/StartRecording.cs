using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Sends START_RECORD to OpenEphys. Passes the trigger through.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class StartRecording
{
    [Description("Endpoint of the OE session; empty = auto-discovery.")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("Recording root directory.")]
    public string Directory { get; set; } = string.Empty;

    [Description("File name prefix.")]
    public string Prefix { get; set; } = string.Empty;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Do(_x => CmdSender.SendStartRecord(session.Transport, Directory, Prefix, out _))
                .Finally(() => SessionRegistry.Release(Endpoint, session));
        });
    }
}
