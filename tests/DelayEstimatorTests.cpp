#include <gtest/gtest.h>

#include <juce_dsp/juce_dsp.h>

#include "DSP/DelayEstimator.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{
// 分数遅延つきの信号を作る（線形補間）
std::vector<float> delayed (const std::vector<float>& reference, double delaySamples, float noiseLevel, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> noise (-noiseLevel, noiseLevel);

    std::vector<float> result (reference.size(), 0.0f);

    for (size_t i = 0; i < reference.size(); ++i)
    {
        const auto pos = (double) i - delaySamples;

        if (pos < 0.0)
            continue;

        const auto i0 = (size_t) pos;
        const auto frac = pos - (double) i0;
        const auto i1 = std::min (i0 + 1, reference.size() - 1);
        result[i] = reference[i0] * (1.0f - (float) frac) + reference[i1] * (float) frac + noise (rng);
    }

    return result;
}

std::vector<float> makeExcitation (int count)
{
    // 遅延推定が決まるよう、広帯域の疑似ランダム信号を使う
    std::mt19937 rng { 12345 };
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> signal ((size_t) count);

    for (auto& sample : signal)
        sample = dist (rng) * 0.5f;

    return signal;
}
} // namespace

TEST (DelayEstimator, RecoversIntegerDelay)
{
    const auto count = 16384;
    const auto reference = makeExcitation (count);
    const auto meas = delayed (reference, 37.0, 0.001f, 7);

    const auto result = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, false);

    EXPECT_TRUE (result.valid);
    EXPECT_NEAR (result.delaySamples, 37.0, 0.15);
}

TEST (DelayEstimator, RecoversDelayWithDcOffsets)
{
    constexpr int count = 16384;
    auto reference = makeExcitation (count);
    auto meas = delayed (reference, 53.0, 0.005f, 19);

    for (int i = 0; i < count; ++i)
    {
        reference[(size_t) i] += 0.35f;
        meas[(size_t) i] -= 0.20f;
    }

    const auto result = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, false);
    EXPECT_TRUE (result.valid);
    EXPECT_NEAR (result.delaySamples, 53.0, 0.2);
}

TEST (DelayEstimator, RecoversSubSampleDelay)
{
    const auto count = 16384;
    const auto reference = makeExcitation (count);
    const auto meas = delayed (reference, 137.5, 0.001f, 11);

    const auto result = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, false);

    EXPECT_TRUE (result.valid);
    EXPECT_NEAR (result.delaySamples, 137.5, 0.25);
}

TEST (DelayEstimator, WorksWithPhatAndNoise)
{
    const auto count = 16384;
    const auto reference = makeExcitation (count);
    const auto meas = delayed (reference, 64.0, 0.05f, 23);

    const auto result = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, true);

    EXPECT_TRUE (result.valid);
    EXPECT_NEAR (result.delaySamples, 64.0, 0.3);
}

TEST (DelayEstimator, NegativeDelayDetected)
{
    const auto count = 16384;
    const auto reference = makeExcitation (count);
    // Meas の方が 12 サンプル早い → ref = delayed(meas, 12)
    const auto meas = makeExcitation (count);
    const auto ref = delayed (meas, 12.0, 0.001f, 31);

    const auto result = eeq::dsp::estimateDelay (ref.data(), meas.data(), count, false);

    EXPECT_TRUE (result.valid);
    EXPECT_NEAR (result.delaySamples, -12.0, 0.2);
}

TEST (DelayEstimator, SilenceIsInvalid)
{
    const auto count = 4096;
    const std::vector<float> reference ((size_t) count, 0.0f);
    const std::vector<float> meas ((size_t) count, 0.0f);

    const auto result = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, false);

    EXPECT_FALSE (result.valid);
}

TEST (DelayEstimator, PeakToSidelobeRatioReported)
{
    const auto count = 16384;
    const auto reference = makeExcitation (count);
    const auto meas = delayed (reference, 100.0, 0.001f, 99);

    const auto result = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, false);

    EXPECT_TRUE (result.valid);
    EXPECT_GT (result.peakToSidelobe, 10.0); // クリーンな信号ならサイドローブは十分低い
}

TEST (DelayEstimator, MinConfidenceCannotBypassNoiseRejection)
{
    // 純粋なノイズ同士の相関は低く、呼び出し側の閾値を下げても安全条件で棄却する
    const auto count = 4096;
    std::mt19937 rng1 (1), rng2 (2);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> noise1 ((size_t) count), noise2 ((size_t) count);
    for (size_t i = 0; i < (size_t) count; ++i)
    {
        noise1[i] = dist (rng1) * 0.1f;
        noise2[i] = dist (rng2) * 0.1f;
    }

    // ノイズ同士の相関は低い（confidence ≈ 1）
    const auto resultNoise = eeq::dsp::estimateDelay (noise1.data(), noise2.data(), count, false);

    // 閾値 5.0 では invalid になる
    EXPECT_FALSE (resultNoise.valid);

    // 呼び出し側が閾値を下げても、無相関ノイズを有効な遅延として扱わない
    const auto resultLowThresh = eeq::dsp::estimateDelay (noise1.data(), noise2.data(), count, false, 0.5);
    EXPECT_FALSE (resultLowThresh.valid);
}

TEST (DelayEstimator, RejectsUncorrelatedNoiseAtMeasurementSize)
{
    constexpr int count = 32768;

    for (unsigned seed = 0; seed < 20; ++seed)
    {
        std::mt19937 rng1 (1000u + seed), rng2 (2000u + seed);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
        std::vector<float> ref ((size_t) count), meas ((size_t) count);

        for (int i = 0; i < count; ++i)
        {
            ref[(size_t) i] = dist (rng1);
            meas[(size_t) i] = dist (rng2);
        }

        const auto result = eeq::dsp::estimateDelay (ref.data(), meas.data(), count, false);
        EXPECT_FALSE (result.valid) << "seed=" << seed << ", confidence=" << result.confidence
                                    << ", peakToSidelobe=" << result.peakToSidelobe;
    }
}

TEST (DelayEstimator, ConstantSignalIsInvalidEvenWithLowThreshold)
{
    constexpr int count = 4096;
    const std::vector<float> ref ((size_t) count, 0.35f);
    const std::vector<float> meas ((size_t) count, -0.20f);

    const auto result = eeq::dsp::estimateDelay (ref.data(), meas.data(), count, false, 0.5);
    EXPECT_FALSE (result.valid);
}

TEST (DelayEstimator, PhatImprovesPeakSharpness)
{
    const auto count = 16384;
    const auto reference = makeExcitation (count);
    const auto meas = delayed (reference, 200.0, 0.05f, 17);

    const auto resultNormal = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, false);
    const auto resultPhat = eeq::dsp::estimateDelay (reference.data(), meas.data(), count, true);

    EXPECT_TRUE (resultPhat.valid);
    // PHAT はピークを鋭くするため peakToSidelobe が向上することが多い
    EXPECT_GE (resultPhat.peakToSidelobe, resultNormal.peakToSidelobe * 0.8);
}
