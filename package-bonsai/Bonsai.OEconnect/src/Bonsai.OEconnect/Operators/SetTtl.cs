using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Latches a TTL output line high or low on the acquisition board, via the " +
    "OEconnect plugin (Bonsai never talks to board firmware directly). The line " +
    "stays in that state until you change it - use PulseTtl for a timed pulse. " +
    "Fire-and-forget: the command is queued and applied at the plugin's next " +
    "acquisition callback (~1 ms at 30 kHz), and this node does not wait for the " +
    "acknowledgement. WARNING: the board adapters are still stubs, so no physical " +
    "line moves yet - the command round-trips but has no hardware effect.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class SetTtl
{
    [Description(
        "Which OE session to command. Empty = auto-discovery (newest live " +
        "plugin). Otherwise \"shm://...\" or \"tcp://host:5557|tcp://host:5558\".")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("Zero-based TTL output line on the board (Rhythm FPGA exposes 8).")]
    public byte Line { get; set; }

    [Description("Level to drive: 1 = high (rising edge), 0 = low (falling edge).")]
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
