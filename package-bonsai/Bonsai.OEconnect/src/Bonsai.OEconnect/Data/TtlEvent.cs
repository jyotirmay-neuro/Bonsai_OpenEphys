namespace Bonsai.OEconnect.Data;

public readonly struct TtlEvent
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }
    public byte Line { get; init; }
    public byte Edge { get; init; }      // 0 = falling, 1 = rising
    public byte BoardId { get; init; }
}
