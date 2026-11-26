#include "DelayEstimator.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace eeq::dsp
{

DelayEstimate estimateDelay (const float* ref, const float* meas, int numSamples, bool usePhat,
                             double minConfidence)
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

    // DCオフセットや一定値だけの入力が相関ピークを作らないよう平均値を除く
    auto removeMean = [&size] (std::vector<float>& buffer)
    {
        double sum = 0.0;

        for (int i = 0; i < size; ++i)
            sum += (double) buffer[(size_t) i];

        const auto mean = (float) (sum / (double) size);

        for (int i = 0; i < size; ++i)
            buffer[(size_t) i] -= mean;
    };

    removeMean (refBuf);
    removeMean (measBuf);

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
    // ゼロラグ（i=0）を基準に、正遅延は前半、負遅延は後半に現れる
    int peakIndex = 0;
    float peakValue = 0.0f;
    double sumAbs = 0.0;
    float maxSidelobe = 0.0f;

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

    // 最大サイドローブを探す。ピーク bin だけでなくメインローブ全体（前後数bin）を
    // ガード区間として除外しないと、隣接 bin が常に最大サイドローブになり
    // 判別指標として機能しない
    constexpr int guardBins = 4;

    for (int i = 0; i < size; ++i)
    {
        const auto linearDist = std::abs (i - peakIndex);
        const auto circularDist = std::min (linearDist, size - linearDist);

        if (circularDist <= guardBins)
            continue;

        const auto v = std::abs (measBuf[(size_t) i]);
        if (v > maxSidelobe)
            maxSidelobe = v;
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

    // 負の遅延（Meas が進んでいる）へ折り返す
    if (delaySamples > (double) size / 2.0)
        delaySamples -= (double) size;

    // 無相関ノイズの最大ピークは信号長に応じて増えるため、単純な固定閾値では棄却できない。
    // 独立ガウス雑音の最大値近似 sqrt(2 ln N) と平均絶対値 sqrt(2/pi) の比に余裕を持たせる。
    const auto meanAbs = sumAbs / (double) size;
    const auto confidenceRatio = meanAbs > 0.0 ? (double) peakValue / meanAbs : 0.0;
    const auto sidelobeRatioDb = maxSidelobe > 0.0f ? 20.0 * std::log10 ((double) peakValue / maxSidelobe) : 60.0;
    const auto noiseConfidenceFloor = 1.8 * std::sqrt (2.0 * std::log ((double) size))
                                    / std::sqrt (2.0 / juce::MathConstants<double>::pi);

    result.delaySamples = delaySamples;
    result.confidence = confidenceRatio;
    result.peakToSidelobe = sidelobeRatioDb;
    result.valid = result.confidence >= std::max (minConfidence, noiseConfidenceFloor)
                && result.peakToSidelobe >= 6.0;

    return result;
}

} // namespace eeq::dsp
