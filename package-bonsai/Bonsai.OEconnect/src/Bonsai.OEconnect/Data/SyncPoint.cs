namespace Bonsai.OEconnect.Data;

public readonly struct SyncPoint
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }
    public double FpgaSampleRateHz { get; init; }
}
