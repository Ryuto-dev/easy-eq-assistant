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
