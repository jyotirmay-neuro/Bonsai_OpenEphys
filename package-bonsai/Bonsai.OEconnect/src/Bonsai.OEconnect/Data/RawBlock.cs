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

    public RawBlock(ulong sampleIndex, ulong hostQpcTicks, ushort streamId,
                    int numChannels, int numSamples, ReadOnlyMemory<short> samples)
    {
        SampleIndex = sampleIndex;
        HostQpcTicks = hostQpcTicks;
        StreamId = streamId;
        NumChannels = numChannels;
        NumSamples = numSamples;
        Samples = samples;
    }

    /// <summary>Returns an owning copy backed by <c>new short[]</c>.</summary>
    public RawBlock Clone() =>
        new(SampleIndex, HostQpcTicks, StreamId, NumChannels, NumSamples,
            Samples.ToArray());
}
