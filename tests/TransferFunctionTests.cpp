#include <gtest/gtest.h>

#include "DSP/TransferFunctionAccumulator.h"

#include <cmath>
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

TEST (TransferFunctionAccumulator, ResetClearsAverages)
{
    constexpr int fftSize = 2048;

    eeq::dsp::TransferFunctionAccumulator accumulator;
    accumulator.configure (fftSize, 48000.0, { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                           juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> ref ((size_t) fftSize, 0.0f);
    std::vector<float> meas ((size_t) fftSize, 0.0f);

    accumulator.feedSamples (ref.data(), meas.data(), fftSize);
    EXPECT_GT (accumulator.getAverages(), 0);

    accumulator.resetAverages();
    EXPECT_EQ (accumulator.getAverages(), 0);

    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);
    EXPECT_EQ (frame.averages, 0);
}
