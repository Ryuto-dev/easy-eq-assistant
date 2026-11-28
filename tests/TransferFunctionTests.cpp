#include <gtest/gtest.h>

#include "DSP/TransferFunctionAccumulator.h"

#include <cmath>
#include <limits>
#include <random>
#include <vector>

// 仕様 7.2「既知の応答で検証する」: 理論値が既知の信号（ゲインと遅延を持つ系）を
// 入力し、H1推定がそれを正しく復元できることを確認する。

TEST (TransferFunctionAccumulator, RecoversKnownGainDelayAndPhase)
{
    constexpr int fftSize = 4096;
    constexpr double sr = 48000.0;
    constexpr int delaySamples = 16;
    constexpr float gain = 0.5f;
    constexpr int bin = 100;
    constexpr int hop = fftSize / 2;
    constexpr int hops = 64;

    eeq::dsp::TransferFunctionAccumulator accumulator;
    accumulator.configure (fftSize, sr, { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                           juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> ref ((size_t) hop);
    std::vector<float> meas ((size_t) hop);
    std::vector<float> history ((size_t) hop * hops, 0.0f);

    // 基準信号: ビン100に正確に乗る正弦波（窓のリークが理論どおりになる）
    // 測定信号: 基準を16サンプル遅らせて 0.5 倍したもの → H = 0.5·e^(-jωτ)
    long long n = 0;

    for (int h = 0; h < hops; ++h)
    {
        for (int i = 0; i < hop; ++i, ++n)
        {
            const auto sample = (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                  * (double) bin * (double) n / (double) fftSize);
            history[(size_t) n] = sample;
            ref[(size_t) i] = sample;
            meas[(size_t) i] = n >= delaySamples ? gain * history[(size_t) n - delaySamples] : 0.0f;
        }

        accumulator.feedSamples (ref.data(), meas.data(), hop);
    }

    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);

    ASSERT_EQ (frame.fftSize, fftSize);
    EXPECT_EQ (frame.averages, hops - 1);

    // 理論値: 振幅 0.5 (=-6.02 dB)、位相 -360°·τ·f/fs = -140.625°
    const auto expectedPhaseDeg = -360.0 * (double) delaySamples * (double) bin / (double) fftSize;

    EXPECT_NEAR (frame.magnitudeDb[(size_t) bin], juce::Decibels::gainToDecibels (gain), 0.05);
    EXPECT_NEAR (frame.phaseDeg[(size_t) bin], expectedPhaseDeg, 0.5);
    EXPECT_GT (frame.coherence[(size_t) bin], 0.99f);

    // 基準信号のエネルギーがない帯域では H1 は不定（0/0）になるのが正しい挙動。
    // その領域は「Refパワーがピークより十分低い」ことで判別できる
    EXPECT_LT (frame.refPowerDb[(size_t) bin + 20], frame.refPowerDb[(size_t) bin] - 60.0f);
}

TEST (TransferFunctionAccumulator, SilentReferenceFramesAreNotAveraged)
{
    // 励起を止めた間（Ref が無音）のフレームを平均に入れると、部屋の騒音だけが積もってコヒーレンスが 0 へ落ちる
    constexpr int fftSize = 2048;
    eeq::dsp::TransferFunctionAccumulator accumulator;
    accumulator.configure (fftSize, 48000.0, { eeq::dsp::AveragingConfig::Mode::fifo, 8 },
                           juce::dsp::WindowingFunction<float>::hann);

    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 0.1f);
    std::vector<float> ref ((size_t) fftSize * 8), meas ((size_t) fftSize * 8);
    for (size_t i = 0; i < ref.size(); ++i)
        meas[i] = ref[i] = noise (rng);

    accumulator.feedSamples (ref.data(), meas.data(), (int) ref.size());
    const auto averagesWhilePlaying = accumulator.getAverages();
    ASSERT_GT (averagesWhilePlaying, 2);

    std::vector<float> silentRef (ref.size(), 0.0f), roomNoise (ref.size());
    for (auto& v : roomNoise)
        v = noise (rng);

    accumulator.feedSamples (silentRef.data(), roomNoise.data(), (int) silentRef.size());
    EXPECT_EQ (accumulator.getAverages(), averagesWhilePlaying);

    // 平均に入っていれば FIFO 8 枠がすべて無音フレームになり γ² ≈ 0。入るのは鳴り終わりをまたぐ数フレームだけ
    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);
    EXPECT_GT (frame.coherence[(size_t) fftSize / 8], 0.7f);
}

TEST (TransferFunctionAccumulator, ResetClearsAverages)
{
    constexpr int fftSize = 2048;

    eeq::dsp::TransferFunctionAccumulator accumulator;
    accumulator.configure (fftSize, 48000.0, { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                           juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> ref ((size_t) fftSize, 0.0f);
    std::vector<float> meas ((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        ref[(size_t) i] = meas[(size_t) i] = 0.5f * (float) std::sin (0.1 * i);

    accumulator.feedSamples (ref.data(), meas.data(), fftSize);
    EXPECT_GT (accumulator.getAverages(), 0);

    accumulator.resetAverages();
    EXPECT_EQ (accumulator.getAverages(), 0);

    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);
    EXPECT_EQ (frame.averages, 0);
}

// #119 (1): NaN / Inf が1サンプル入っても Welch 平均を汚染しない（0 に置き換える）
TEST (TransferFunctionAccumulator, NonFiniteInputIsSanitized)
{
    constexpr int fftSize = 2048;
    constexpr int bin = 64;

    eeq::dsp::TransferFunctionAccumulator accumulator;
    accumulator.configure (fftSize, 48000.0, { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                           juce::dsp::WindowingFunction<float>::hann);

    const auto total = fftSize * 8;
    std::vector<float> ref ((size_t) total), meas ((size_t) total);

    for (int i = 0; i < total; ++i)
    {
        ref[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * bin * i / (double) fftSize);
        meas[(size_t) i] = 0.5f * ref[(size_t) i];
    }

    meas[(size_t) fftSize + 100] = std::numeric_limits<float>::quiet_NaN();
    ref[(size_t) fftSize * 3] = std::numeric_limits<float>::infinity();

    accumulator.feedSamples (ref.data(), meas.data(), total);

    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);

    EXPECT_EQ (accumulator.getNonFiniteSampleCount(), 2);

    for (size_t k = 0; k < frame.transfer.size(); ++k)
    {
        ASSERT_TRUE (std::isfinite (frame.magnitudeDb[k])) << k;
        ASSERT_TRUE (std::isfinite (frame.coherence[k])) << k;
        ASSERT_TRUE (std::isfinite (frame.transfer[k].real()) && std::isfinite (frame.transfer[k].imag())) << k;
    }

    // 1サンプルの欠落でも推定値はほぼ変わらない（−6.02 dB）
    EXPECT_NEAR (frame.magnitudeDb[(size_t) bin], -6.02f, 0.1f);
}

// #119 (2): 平均1回のコヒーレンスは定義上 1.0 になるので未確定（0）として返す
TEST (TransferFunctionAccumulator, CoherenceIsUndefinedWithSingleAverage)
{
    constexpr int fftSize = 2048;

    eeq::dsp::TransferFunctionAccumulator accumulator;
    accumulator.configure (fftSize, 48000.0, { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                           juce::dsp::WindowingFunction<float>::hann);

    // 無相関なノイズ同士: 本来 γ² ≈ 0 だが、平均1回では全 bin 1.0 になっていた
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    std::vector<float> ref ((size_t) fftSize * 4), meas ((size_t) fftSize * 4);

    for (size_t i = 0; i < ref.size(); ++i)
    {
        ref[i] = dist (rng);
        meas[i] = dist (rng);
    }

    accumulator.feedSamples (ref.data(), meas.data(), fftSize); // ちょうど1フレーム

    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);

    ASSERT_EQ (frame.averages, 1);
    EXPECT_FALSE (frame.hasReliableCoherence());

    for (const auto c : frame.coherence)
        ASSERT_EQ (c, 0.0f);

    // 2回目以降は通常どおり計算する（無相関なので低い値になる）
    accumulator.feedSamples (ref.data() + fftSize, meas.data() + fftSize, fftSize * 3);
    accumulator.copyResult (frame);

    ASSERT_GE (frame.averages, 2);
    EXPECT_TRUE (frame.hasReliableCoherence());

    double mean = 0.0;

    for (const auto c : frame.coherence)
        mean += c;

    mean /= (double) frame.coherence.size();
    EXPECT_GT (mean, 0.0);
    EXPECT_LT (mean, 0.6);
}
