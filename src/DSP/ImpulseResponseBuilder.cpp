#include "ImpulseResponseBuilder.h"

#include "DelayEstimator.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace eeq::dsp
{

namespace
{
// bin ごとの重み（0..1）。無効な bin は 0、有効 bin は無効 bin からの距離に応じて余弦テーパー
std::vector<float> binWeights (const SpectrumFrame& frame, const ImpulseResponseOptions& options)
{
    const auto halfBins = frame.transfer.size();
    std::vector<bool> valid (halfBins, true);

    if (frame.refPowerDb.size() == halfBins)
    {
        float refPeak = -1.0e30f;

        for (const auto level : frame.refPowerDb)
            if (std::isfinite (level))
                refPeak = std::max (refPeak, level);

        for (size_t k = 0; k < halfBins; ++k)
            if (! std::isfinite (frame.refPowerDb[k]) || frame.refPowerDb[k] < refPeak - options.refFloorDbBelowPeak)
                valid[k] = false;
    }

    // 平均1回ではコヒーレンスが確定しないので判定しない（#119）
    if (frame.averages >= 2 && frame.coherence.size() == halfBins)
        for (size_t k = 0; k < halfBins; ++k)
            if (! (frame.coherence[k] >= options.minCoherence))
                valid[k] = false;

    for (size_t k = 0; k < halfBins; ++k)
    {
        const auto& h = frame.transfer[k];

        if (! std::isfinite (h.real()) || ! std::isfinite (h.imag()))
            valid[k] = false;
    }

    // 無効 bin までの距離（両方向）を求めてテーパーをかける
    const auto taper = options.taperBins > 0 ? options.taperBins
                                             : std::max (4, frame.fftSize / 1024);
    std::vector<int> distance (halfBins, std::numeric_limits<int>::max() / 2);
    int last = -1;

    for (size_t k = 0; k < halfBins; ++k)
    {
        if (! valid[k])
            last = (int) k;

        if (last >= 0)
            distance[k] = (int) k - last;
    }

    last = -1;

    for (size_t k = halfBins; k-- > 0;)
    {
        if (! valid[k])
            last = (int) k;

        if (last >= 0)
            distance[k] = std::min (distance[k], last - (int) k);
    }

    std::vector<float> weights (halfBins, 1.0f);

    for (size_t k = 0; k < halfBins; ++k)
    {
        if (! valid[k])
            weights[k] = 0.0f;
        else if (distance[k] < taper)
            weights[k] = (float) (0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi
                                                         * (double) distance[k] / (double) taper)));
    }

    return weights;
}
} // namespace

ImpulseResponse buildImpulseResponse (const SpectrumFrame& frame, const ImpulseResponseOptions& options)
{
    ImpulseResponse out;
    out.sampleRate = frame.sampleRate;

    const auto halfBins = (int) frame.transfer.size();

    if (halfBins < 2 || frame.fftSize <= 0)
        return out;

    const auto n = frame.fftSize;
    out.fftSize = n;

    if (halfBins != n / 2 + 1)
        return out; // SpectrumFrame とサイズが整合していない

    juce::dsp::FFT fft ([n] { int order = 0; while ((1 << (order + 1)) <= n) ++order; return order; }());

    // エルミート対称なフルスペクトルを組み立てて実数逆FFTへ
    // Ref にエネルギーが無い bin・γ² が低い bin は 0 にしてテーパーをかける（#115）
    const auto weights = binWeights (frame, options);
    std::vector<float> buffer ((size_t) n * 2, 0.0f);

    for (int k = 0; k <= n / 2; ++k)
    {
        const auto w = weights[(size_t) k];
        buffer[(size_t) k * 2] = w > 0.0f ? frame.transfer[(size_t) k].real() * w : 0.0f;
        buffer[(size_t) k * 2 + 1] = w > 0.0f ? frame.transfer[(size_t) k].imag() * w : 0.0f;
    }

    for (int k = n / 2 + 1; k < n; ++k)
    {
        buffer[(size_t) k * 2] = buffer[(size_t) (n - k) * 2];
        buffer[(size_t) k * 2 + 1] = -buffer[(size_t) (n - k) * 2 + 1];
    }

    fft.performRealOnlyInverseTransform (buffer.data());

    // JUCE の実数逆FFTは 1/N 正規化済み（H=1 が単位インパルスになる）
    out.samples.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
        out.samples[(size_t) i] = buffer[(size_t) i];

    // ピーク位置（サブサンプル補間）
    int peakIndex = 0;
    double peakAbs = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto a = std::abs ((double) out.samples[(size_t) i]);

        if (a > peakAbs)
        {
            peakAbs = a;
            peakIndex = i;
        }
    }

    // 符号付きの値でサブサンプル補間する（#118）
    const auto fractional = subsamplePeakOffset (out.samples.data(), n, peakIndex);

    out.peakTimeSamples = (double) peakIndex + fractional;
    out.peakValue = out.samples[(size_t) peakIndex];

    return out;
}

} // namespace eeq::dsp
