using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

public abstract class SessionSource<T>
{
    [Description(
        "Which OpenEphys session to attach to. Leave EMPTY for auto-discovery: " +
        "Bonsai scans %TEMP%\\oeconnect\\sessions for the newest live plugin and " +
        "verifies its heartbeat. Otherwise give an explicit endpoint - " +
        "\"shm://Local\\oeconnect.<pid>.shm\" for shared memory (same machine, " +
        "sub-millisecond) or \"tcp://host:5557|tcp://host:5558\" for ZMQ " +
        "(data|command sockets, works across machines). All nodes sharing an " +
        "endpoint share one underlying connection.")]
    public string Endpoint { get; set; } = string.Empty;

    protected IObservable<T> Subscribe(Func<Session, IObservable<T>> selector)
    {
        return Observable.Create<T>(observer =>
        {
            Session session;
            try { session = SessionRegistry.Acquire(Endpoint); }
            catch (Exception ex) { observer.OnError(ex); return () => { }; }

            var sub = selector(session).Subscribe(observer);
            return () => { sub.Dispose(); SessionRegistry.Release(Endpoint, session); };
        });
    }
}
