using System;
using System.ComponentModel;
using System.Reactive.Linq;
using System.Threading;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Emits a SessionStatus snapshot once per second: whether the transport is " +
    "connected, which transport is in use, total frames received, and how many " +
    "frames the plugin reported dropping. Use this to confirm data is actually " +
    "flowing - a steadily rising FrameCount with DropCount at zero means the " +
    "pipeline is healthy.")]
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
