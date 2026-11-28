#include "DelayEstimator.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace eeq::dsp
{

DelayEstimate estimateDelay (const float* ref, const float* meas, int numSamples, bool usePhat,
                             double minConfidence, int mainLobeGuard)
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
    const int guardBins = std::max (4, std::min (mainLobeGuard, numSamples / 8));

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

    // 符号付きの値でサブサンプル補間する（仕様 3.3 / #118）
    const auto fractional = subsamplePeakOffset (measBuf.data(), size, peakIndex);

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
    // ピーク/サイドローブ比が低い（同じくらいの高さの山が別にある）推定は曖昧なので捨てる。
    // ただし相関がノイズ下限の 3 倍以上と非常に強ければ、最大の山は実在の到達音なので採る。
    // 実機では 2 つのスピーカー・ドライバ間の時間差や机の反射で PSR が 2 dB 前後でも、
    // 遅延は 0.1 サンプル以内で安定していた（MacBook の内蔵マイク＋スピーカー: confidence 79）
    const auto floor = std::max (minConfidence, noiseConfidenceFloor);
    result.valid = result.confidence >= floor
                && (result.peakToSidelobe >= 6.0 || result.confidence >= 3.0 * floor);

    return result;
}

} // namespace eeq::dsp
