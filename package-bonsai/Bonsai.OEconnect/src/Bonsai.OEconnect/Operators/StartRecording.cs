using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Tells OpenEphys to start recording, once per element received, and passes the " +
    "element through unchanged. A \"slow\" command: the plugin acknowledges " +
    "immediately with PENDING and does the disk work on a worker thread, so the " +
    "acquisition thread is never blocked. Recording starts on every Record Node in " +
    "the OE signal chain.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class StartRecording
{
    [Description(
        "Which OE session to command. Empty = auto-discovery (newest live " +
        "plugin). Otherwise \"shm://...\" or \"tcp://host:5557|tcp://host:5558\".")]
    public string Endpoint { get; set; } = string.Empty;

    [Description(
        "Recording root directory, resolved on the OpenEphys machine - not " +
        "Bonsai's filesystem. Applied to every Record Node in the chain. Leave " +
        "empty to keep whatever directory the OE GUI is already configured with.")]
    public string Directory { get; set; } = string.Empty;

    [Description(
        "Text prepended to the recording directory name. OpenEphys has no " +
        "per-file prefix: it names recordings <prepend><base><append>, and this " +
        "sets the prepend text. Leave empty to keep the OE GUI's naming settings.")]
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
