using System;

namespace Bonsai.OEconnect.Data;

/// <summary>
/// One block of continuous samples produced by an OE source node.
///
/// IMPORTANT: <see cref="Samples"/> wraps a pooled buffer that is valid ONLY
/// inside the OnNext invocation that delivered this block. Clone via
/// <see cref="Clone"/> or <c>Samples.ToArray()</c> if you need to retain it.
/// </summary>
public readonly struct RawBlock
{
    public ulong SampleIndex { get; }
    public ulong HostQpcTicks { get; }
    public ushort StreamId { get; }
    public int NumChannels { get; }
    public int NumSamples { get; }
    public ReadOnlyMemory<short> Samples { get; }

    /// <summary>
    /// Which OpenEphys DataStream produced this block. One block is published per
    /// stream per acquisition callback, so with a multi-stream source (Neuropixels
    /// AP + LFP) blocks with different <see cref="SourceId"/> interleave, each with
    /// its own channel count, sample rate and <see cref="SampleIndex"/> clock.
    /// Match it against <see cref="SyncPoint.Streams"/> to recover those.
    /// </summary>
    public byte SourceId { get; }

    /// <summary>Retained overload; equivalent to <c>SourceId = 0</c>.</summary>
    public RawBlock(ulong sampleIndex, ulong hostQpcTicks, ushort streamId,
                    int numChannels, int numSamples, ReadOnlyMemory<short> samples)
        : this(sampleIndex, hostQpcTicks, streamId, numChannels, numSamples, samples, 0)
    {
    }

    public RawBlock(ulong sampleIndex, ulong hostQpcTicks, ushort streamId,
                    int numChannels, int numSamples, ReadOnlyMemory<short> samples,
                    byte sourceId)
    {
        SampleIndex = sampleIndex;
        HostQpcTicks = hostQpcTicks;
        StreamId = streamId;
        NumChannels = numChannels;
        NumSamples = numSamples;
        Samples = samples;
        SourceId = sourceId;
    }

    /// <summary>Returns an owning copy backed by <c>new short[]</c>.</summary>
    public RawBlock Clone() =>
        new(SampleIndex, HostQpcTicks, StreamId, NumChannels, NumSamples,
            Samples.ToArray(), SourceId);
}
