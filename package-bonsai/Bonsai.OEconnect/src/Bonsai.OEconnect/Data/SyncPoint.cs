using System;

namespace Bonsai.OEconnect.Data;

public readonly struct SyncPoint
{
    public ulong SampleIndex { get; init; }
    public ulong HostQpcTicks { get; init; }

    /// <summary>Sample rate of stream 0. For a multi-stream source see <see cref="Streams"/>.</summary>
    public double FpgaSampleRateHz { get; init; }

    /// <summary>
    /// One entry per active DataStream (spec §3.1 <c>stream_meta[]</c>), indexed by
    /// <see cref="StreamMeta.SourceId"/>. Empty when the producer advertises none.
    /// </summary>
    public ReadOnlyMemory<StreamMeta> Streams { get; init; }
}
