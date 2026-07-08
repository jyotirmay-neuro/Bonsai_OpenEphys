using System;
using System.ComponentModel;
using System.Reactive.Linq;
using Bonsai;
using Bonsai.OEconnect.Data;
using OpenCV.Net;

namespace Bonsai.OEconnect.Operators;

/// <summary>
/// Converts <see cref="RawBlock"/> into a Bonsai.Dsp <see cref="Mat"/> (rows = channels, cols = samples).
/// The output owns a fresh allocation; the input pooled buffer is released after copy.
/// </summary>
[Combinator]
[Description(
    "Converts a RawBlock into an OpenCV Mat (rows = channels, cols = samples, " +
    "depth = S16) so it can be plotted or processed by the Bonsai.Dsp operators. " +
    "This is the node to attach a visualizer to: right-click it and choose a " +
    "matrix/waveform visualizer to watch the signal live. The Mat owns a fresh " +
    "copy, so unlike the incoming RawBlock it is safe to retain. Values are int16 " +
    "ADC counts - multiply by the channel's bitVolts to get microvolts.")]
[WorkflowElementCategory(ElementCategory.Transform)]
public class ToMat
{
    public IObservable<Mat> Process(IObservable<RawBlock> source)
    {
        return source.Select(b =>
        {
            var mat = new Mat(b.NumChannels, b.NumSamples, Depth.S16, 1);
            var span = b.Samples.Span;
            unsafe
            {
                fixed (short* src = span)
                {
                    Buffer.MemoryCopy(src, mat.Data.ToPointer(),
                                      span.Length * sizeof(short),
                                      span.Length * sizeof(short));
                }
            }
            return mat;
        });
    }
}
