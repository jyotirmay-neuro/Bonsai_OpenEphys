namespace Bonsai.OEconnect.Data;

/// <summary>
/// Describes one OpenEphys DataStream, as advertised by the periodic SYNC frame
/// (spec §3.1 <c>stream_meta[]</c>). Lets a late-joining subscriber reconstruct
/// each stream's geometry without waiting for a data block.
/// </summary>
public readonly struct StreamMeta
{
    /// <summary>Matches <see cref="RawBlock.SourceId"/> on blocks from this stream.</summary>
    public byte SourceId { get; init; }

    /// <summary>Channels in this stream's blocks.</summary>
    public ushort NumChannels { get; init; }

    /// <summary>
    /// Acquisition rate of this stream. Streams differ: a Neuropixels probe emits
    /// AP at 30 kHz alongside LFP at 2.5 kHz, so each has its own sample clock.
    /// </summary>
    public double SampleRateHz { get; init; }
}
