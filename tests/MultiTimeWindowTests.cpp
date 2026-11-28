#include <gtest/gtest.h>

#include "DSP/MultiTimeWindow.h"

#include <cmath>
#include <random>
#include <vector>

// MTW（ロードマップ B-2）: 直接音の 15 ms 後に −6 dB の反射がある系。
// 単一の長い窓（16384 = 341 ms）は反射を含むので高域にコムフィルタの凹凸（±3.5 dB）が出る。
// 高域を短い窓（512 = 10.7 ms）で見る MTW では反射が窓の外に出て、凹凸が小さくなる。
namespace
{
double rippleDb (const eeq::dsp::SpectrumFrame& frame, double lowHz, double highHz)
{
    const auto binHz = frame.sampleRate / frame.fftSize;
    double sum = 0.0, sumSq = 0.0;
    int n = 0;

    for (size_t k = 1; k < frame.magnitudeDb.size(); ++k)
    {
        const auto f = (double) k * binHz;
        if (f < lowHz || f >= highHz)
            continue;
        sum += frame.magnitudeDb[k];
        sumSq += (double) frame.magnitudeDb[k] * frame.magnitudeDb[k];
        ++n;
    }

    const auto mean = sum / n;
    return std::sqrt (std::max (0.0, sumSq / n - mean * mean));
}
} // namespace

TEST (MultiTimeWindow, ShortWindowsRemoveTheLateReflectionRippleAtHighFrequencies)
{
    constexpr double rate = 48000.0;
    constexpr int mainSize = 16384;
    constexpr int reflection = 720; // 15 ms
    const auto total = (int) (rate * 8.0);

    std::mt19937 rng (5);
    std::normal_distribution<float> noise (0.0f, 0.2f);
    std::vector<float> ref ((size_t) total), meas ((size_t) total);
    for (auto& v : ref)
        v = noise (rng);
    for (int i = 0; i < total; ++i)
        meas[(size_t) i] = ref[(size_t) i] + (i >= reflection ? 0.5f * ref[(size_t) (i - reflection)] : 0.0f);

    eeq::dsp::AveragingConfig averaging;
    averaging.mode = eeq::dsp::AveragingConfig::Mode::fifo;
    averaging.maxAverages = 16;

    eeq::dsp::TransferFunctionAccumulator main;
    main.configure (mainSize, rate, averaging, juce::dsp::WindowingFunction<float>::hann);
    eeq::dsp::MultiTimeWindow mtw;
    mtw.configure (mainSize, rate, averaging, juce::dsp::WindowingFunction<float>::hann);

    for (int start = 0; start + 512 <= total; start += 512)
    {
        main.feedSamples (&ref[(size_t) start], &meas[(size_t) start], 512);
        mtw.feedSamples (&ref[(size_t) start], &meas[(size_t) start], 512);
    }

    eeq::dsp::SpectrumFrame single, composite;
    main.copyResult (single);
    mtw.compose (single, composite);

    const auto singleRipple = rippleDb (single, 4000.0, 16000.0);
    const auto mtwRipple = rippleDb (composite, 4000.0, 16000.0);

    EXPECT_GT (singleRipple, 2.0) << "the long window should see the comb filter";
    EXPECT_LT (mtwRipple, singleRipple * 0.5) << "single " << singleRipple << " dB, MTW " << mtwRipple << " dB";

    // 低域（メインの窓が受け持つ）はそのまま
    const auto binHz = rate / mainSize;
    for (size_t k = (size_t) (40.0 / binHz); k < (size_t) (120.0 / binHz); ++k)
        EXPECT_NEAR (composite.magnitudeDb[k], single.magnitudeDb[k], 1.0e-4f);

    // 境目は 176 / 703 / 2812 Hz 付近（48 kHz）
    EXPECT_NEAR (mtw.crossoverFor (0), 175.8, 0.5);
    EXPECT_NEAR (mtw.crossoverFor (2), 2812.5, 0.5);
}
