using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Sessions;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Drives a TTL line high, then automatically returns it low after " +
    "WidthMicroseconds. Sent as a single PULSE_TTL command; the plugin asserts " +
    "the edge on its next acquisition callback and schedules the falling edge by " +
    "FPGA sample index, so pulse width is sample-accurate rather than " +
    "wall-clock-accurate. Width is rounded up to at least one sample. " +
    "WARNING: the board adapters are still stubs, so no physical line moves yet - " +
    "the command round-trips but has no hardware effect.")]
[WorkflowElementCategory(ElementCategory.Sink)]
public class PulseTtl
{
    [Description(
        "Which OE session to command. Empty = auto-discovery (newest live " +
        "plugin). Otherwise \"shm://...\" or \"tcp://host:5557|tcp://host:5558\".")]
    public string Endpoint { get; set; } = string.Empty;

    [Description("Zero-based TTL output line on the board (Rhythm FPGA exposes 8).")]
    public byte Line { get; set; }

    [Description(
        "Pulse width in microseconds. Converted to a whole number of FPGA " +
        "samples (at 30 kHz, one sample = ~33 us); values below one sample " +
        "period are clamped to one sample.")]
    public uint WidthMicroseconds { get; set; } = 1000;

    public IObservable<TSource> Process<TSource>(IObservable<TSource> source)
    {
        return Observable.Defer(() =>
        {
            var session = SessionRegistry.Acquire(Endpoint);
            return source
                .Do(_x => CmdSender.SendPulseTtl(session.Transport, Line, true, WidthMicroseconds, out _))
                .Finally(() => SessionRegistry.Release(Endpoint, session));
        });
    }
}
