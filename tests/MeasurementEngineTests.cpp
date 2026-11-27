#include <gtest/gtest.h>

#include "DSP/MeasurementEngine.h"

#include <array>
#include <chrono>
#include <thread>

TEST (MeasurementEngine, StartRequestEventuallyAccumulatesFrames)
{
    eeq::dsp::SampleRingBuffer ring (32768);
    eeq::dsp::MeasurementEngine engine (ring);
    engine.configure (256, 48000.0,
                      { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                      juce::dsp::WindowingFunction<float>::hann);

    engine.startMeasurement();
    EXPECT_TRUE (engine.isMeasuring());

    std::array<float, 256> samples {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);

    while (engine.getAverages() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        ring.write (samples.data(), samples.data(), (int) samples.size());
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    EXPECT_GT (engine.getAverages(), 0);
    engine.stopMeasurement();
    EXPECT_FALSE (engine.isMeasuring());
}

#include <cmath>
#include <random>
#include <vector>

namespace
{
// 白色雑音 ref と、それを delay サンプル遅らせた meas を流し、平均が溜まった結果を返す
eeq::dsp::SpectrumFrame measureDelayedNoise (int delay, int refDelayCompensation)
{
    eeq::dsp::SampleRingBuffer ring (1 << 16);
    eeq::dsp::MeasurementEngine engine (ring);
    engine.configure (8192, 48000.0, { eeq::dsp::AveragingConfig::Mode::cumulative, 64 },
                      juce::dsp::WindowingFunction<float>::hann);
    engine.setReferenceDelaySamples (refDelayCompensation);

    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 0.2f);
    std::vector<float> history ((size_t) delay + 1024, 0.0f);
    std::vector<float> ref (1024), meas (1024);
    size_t written = 0;

    // 遅延補償の反映（リセット）を待ってから測定を始める
    std::this_thread::sleep_for (std::chrono::milliseconds (30));
    engine.startMeasurement();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (20);

    while (engine.getAverages() < 40 && std::chrono::steady_clock::now() < deadline)
    {
        if (ring.samplesAvailable() > 1 << 14)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }

        for (size_t i = 0; i < ref.size(); ++i, ++written)
        {
            ref[i] = noise (rng);
            // meas[n] = ref[n - delay]
            history[written % history.size()] = ref[i];
            meas[i] = written >= (size_t) delay ? history[(written - (size_t) delay) % history.size()] : 0.0f;
        }

        ring.write (ref.data(), meas.data(), (int) ref.size());
    }

    // 最後のフレームが公開されるのを待つ
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    eeq::dsp::SpectrumFrame frame;
    engine.copyLatestResult (frame);
    engine.stopMeasurement();
    return frame;
}

double meanMagnitudeDb (const eeq::dsp::SpectrumFrame& f, double lo, double hi)
{
    double sum = 0.0;
    int n = 0;
    const auto binWidth = f.sampleRate / f.fftSize;

    for (size_t k = 0; k < f.transfer.size(); ++k)
        if (k * binWidth >= lo && k * binWidth <= hi)
        {
            sum += 20.0 * std::log10 (std::max (1.0e-9, (double) std::abs (f.transfer[k])));
            ++n;
        }

    return n > 0 ? sum / n : -300.0;
}

double meanCoherence (const eeq::dsp::SpectrumFrame& f, double lo, double hi)
{
    double sum = 0.0;
    int n = 0;
    const auto binWidth = f.sampleRate / f.fftSize;

    for (size_t k = 0; k < f.coherence.size(); ++k)
        if (k * binWidth >= lo && k * binWidth <= hi)
        {
            sum += f.coherence[k];
            ++n;
        }

    return n > 0 ? sum / n : 0.0;
}
} // namespace

// issue #106: 遅延を補償しないと |H1| とコヒーレンスが窓の重なり分だけ落ちる。
// 40 ms（1920 サンプル）・8192 点 Hann で −3.2 dB / γ² 0.48 程度になる。補償すれば 0 dB / γ² ≈ 1
TEST (MeasurementEngine, ReferenceDelayCompensationRemovesWindowMisalignmentBias)
{
    constexpr int delay = 1920;

    const auto uncompensated = measureDelayedNoise (delay, 0);
    ASSERT_GE (uncompensated.averages, 40);
    EXPECT_LT (meanMagnitudeDb (uncompensated, 200.0, 10000.0), -2.5);
    EXPECT_LT (meanCoherence (uncompensated, 200.0, 10000.0), 0.6);

    const auto compensated = measureDelayedNoise (delay, delay);
    ASSERT_GE (compensated.averages, 40);
    EXPECT_NEAR (meanMagnitudeDb (compensated, 200.0, 10000.0), 0.0, 0.1);
    EXPECT_GT (meanCoherence (compensated, 200.0, 10000.0), 0.98);
    EXPECT_DOUBLE_EQ (compensated.refDelaySamples, (double) delay);

    // 位相は織り込み済みの遅延を除いた残り（0 サンプル）なので平ら
    const auto k = (size_t) std::lround (1000.0 / (48000.0 / 8192.0));
    EXPECT_NEAR (std::arg (std::complex<double> (compensated.transfer[k])), 0.0, 0.05);
}
