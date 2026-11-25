#include "DelayEstimator.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace eeq::dsp
{

DelayEstimate estimateDelay (const float* ref, const float* meas, int numSamples, bool usePhat)
{
    DelayEstimate result;

    if (ref == nullptr || meas == nullptr || numSamples < 16)
        return result;

    // 2の冪に制限する（呼び出し側は accumulator と同じ pow2 を渡す想定）
    int order = 0;

    while ((1 << (order + 1)) <= numSamples)
        ++order;

    const auto size = 1 << order;

    if (size != numSamples)
        return result;

    juce::dsp::FFT fft (order);
    std::vector<float> refBuf ((size_t) size * 2, 0.0f);
    std::vector<float> measBuf ((size_t) size * 2, 0.0f);

    std::copy (ref, ref + size, refBuf.begin());
    std::copy (meas, meas + size, measBuf.begin());

    // 入力がほぼ無音なら推定しない
    auto rmsOf = [&size] (const std::vector<float>& buffer)
    {
        double sum = 0.0;

        for (int i = 0; i < size; ++i)
            sum += (double) buffer[(size_t) i] * (double) buffer[(size_t) i];

        return std::sqrt (sum / (double) size);
    };

    if (rmsOf (refBuf) < 1.0e-6 || rmsOf (measBuf) < 1.0e-6)
        return result;

    fft.performRealOnlyForwardTransform (refBuf.data(), true);
    fft.performRealOnlyForwardTransform (measBuf.data(), true);

    // 相互パワー Gxy = M(f)·conj(R(f))。Gxy の逆FFTのピーク位置が Meas の遅れ（サンプル）
    const auto halfBins = size / 2 + 1;

    for (int k = 0; k < halfBins; ++k)
    {
        const auto mr = measBuf[(size_t) k * 2];
        const auto mi = measBuf[(size_t) k * 2 + 1];
        const auto rr = refBuf[(size_t) k * 2];
        const auto ri = refBuf[(size_t) k * 2 + 1];

        // M·conj(R) = (mr + j·mi)(rr - j·ri)
        auto gr = mr * rr + mi * ri;
        auto gi = mi * rr - mr * ri;

        if (usePhat)
        {
            const auto mag = std::sqrt (gr * gr + gi * gi);

            if (mag > 1.0e-12f)
            {
                gr /= mag;
                gi /= mag;
            }
            else
            {
                gr = 0.0f;
                gi = 0.0f;
            }
        }

        measBuf[(size_t) k * 2] = gr;
        measBuf[(size_t) k * 2 + 1] = gi;
    }

    // 残り（k = halfBins .. size-1）はエルミート対称にして実数の逆FFTにする
    for (int k = halfBins; k < size; ++k)
    {
        const auto src = size - k;
        measBuf[(size_t) k * 2] = measBuf[(size_t) src * 2];
        measBuf[(size_t) k * 2 + 1] = -measBuf[(size_t) src * 2 + 1];
    }

    fft.performRealOnlyInverseTransform (measBuf.data());

    // JUCE の実数逆FFTは 1/size 正規化済み（そのまま相互相関の値になる）

    // ピーク探索（循環相関なので負ラグは後半に現れる）
    int peakIndex = 0;
    float peakValue = 0.0f;
    double sumAbs = 0.0;

    for (int i = 0; i < size; ++i)
    {
        const auto v = std::abs (measBuf[(size_t) i]);
        sumAbs += (double) v;

        if (v > peakValue)
        {
            peakValue = v;
            peakIndex = i;
        }
    }

    if (peakValue <= 0.0f)
        return result;

    // 放物線補間でサブサンプル精度（仕様 3.3）
    const auto y0 = std::abs (measBuf[(size_t) ((peakIndex - 1 + size) % size)]);
    const auto y1 = peakValue;
    const auto y2 = std::abs (measBuf[(size_t) ((peakIndex + 1) % size)]);
    const auto denom = y0 - 2.0f * y1 + y2;
    auto fractional = denom != 0.0f ? 0.5f * (y0 - y2) / denom : 0.0f;
    fractional = juce::jlimit (-0.5f, 0.5f, fractional);

    auto delaySamples = (double) peakIndex + (double) fractional;

    if (delaySamples > (double) size / 2.0)
        delaySamples -= (double) size; // 負の遅延（Meas が進んでいる）へ折り返す

    result.delaySamples = delaySamples;
    result.confidence = sumAbs > 0.0 ? (double) peakValue / (sumAbs / (double) size) : 0.0;
    result.valid = result.confidence >= 5.0; // ノイズだけの相互相関（≈1前後）と区別する目安

    return result;
}

} // namespace eeq::dsp
