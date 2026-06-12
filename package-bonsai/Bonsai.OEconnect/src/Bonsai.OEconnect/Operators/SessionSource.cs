using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

public abstract class SessionSource<T>
{
    [Description("Endpoint: empty = auto-discovery; \"tcp://host:port\" = ZMQ; \"shm://...\" = explicit shmem.")]
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
