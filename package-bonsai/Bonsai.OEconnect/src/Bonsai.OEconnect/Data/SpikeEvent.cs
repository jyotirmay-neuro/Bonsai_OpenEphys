using System;

namespace Bonsai.OEconnect.Data;

public readonly struct SpikeEvent
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }
    public ushort ElectrodeId { get; init; }
    public ushort UnitId { get; init; }
    public float Threshold { get; init; }
    public ReadOnlyMemory<short> Waveform { get; init; }
}
