using System;
using System.ComponentModel;
using System.Reactive.Linq;
using System.Threading;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description("Emits SessionStatus snapshots once per second -- liveness, transport, drop count, board name.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class OpenEphysSession : SessionSource<SessionStatus>
{
    public IObservable<SessionStatus> Process()
    {
        return Subscribe(session =>
            Observable.Interval(TimeSpan.FromSeconds(1))
                .Select(_ => new SessionStatus
                {
                    IsConnected = session.Transport.IsConnected,
                    Transport = session.Transport.Name,
                    BoardName = "-",
                    FrameCount = Interlocked.Read(ref session.FrameCount),
                    DropCount  = Interlocked.Read(ref session.DropCount),
                    EstimatedLagMs = 0.0
                }));
    }
}
