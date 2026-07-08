using System;
using System.ComponentModel;
using Bonsai;
using Bonsai.OEconnect.Data;

namespace Bonsai.OEconnect.Operators;

[Combinator]
[Description(
    "Streams continuous broadband samples from OpenEphys, one RawBlock per " +
    "DataStream per acquisition callback (32 samples = ~1.07 ms at 30 kHz). A " +
    "multi-stream source (Neuropixels AP 30 kHz + LFP 2.5 kHz) interleaves blocks " +
    "with different SourceId, channel counts and sample clocks - filter on SourceId " +
    "to isolate one, and look it up in SyncPoints.Streams for its rate. Samples are int16 " +
    "ADC counts in channel-major order; multiply by the channel's bitVolts to " +
    "get microvolts. Feed into ToMat to plot with the Bonsai.Dsp visualizers. " +
    "WARNING: RawBlock.Samples wraps a buffer that is only valid inside the " +
    "OnNext call - use Clone() or Samples.ToArray() before retaining it.")]
[WorkflowElementCategory(ElementCategory.Source)]
public class RawSamples : SessionSource<RawBlock>
{
    public IObservable<RawBlock> Process() => Subscribe(s => s.RawSubject);
}
