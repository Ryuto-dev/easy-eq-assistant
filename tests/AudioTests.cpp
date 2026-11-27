#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "Audio/AudioEngine.h"
#include "Audio/PinkNoiseGenerator.h"
#include "DSP/SweepGenerator.h"

namespace
{

double rmsOf (const std::vector<float>& samples, size_t skip)
{
    double sumSquares = 0.0;
    const auto count = samples.size() > skip ? samples.size() - skip : 0;

    for (size_t i = 0; i < count; ++i)
        sumSquares += (double) samples[i + skip] * (double) samples[i + skip];

    return count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0;
}

// 出力レベルの統一定義（issue #29）: 設定値 = dBFS RMS（ピンクノイズ）。
// 生成器が実効RMS 1.0 になるよう正規化されていることを回帰として固定する
TEST (Audio, PinkNoiseGeneratorIsUnitRms)
{
    eeq::audio::PinkNoiseGenerator generator;
    constexpr int count = 1 << 20;
    std::vector<float> samples ((size_t) count);

    for (int i = 0; i < count; ++i)
        samples[(size_t) i] = generator.nextSample();

    // 立ち上がり（フィルタの初期状態）を捨てて定常RMSを評価する
    const auto rms = rmsOf (samples, 4096);
    EXPECT_NEAR (rms, 1.0, 0.05);
}

// スイープはピーク1.0で生成され、オーディオスレッドが √2 を掛けて実効RMS基準に揃える。
// ピーク1.0の対数スイープの実効RMSが 1/√2 であることを固定する（sin²の時間平均 = 1/2）
TEST (Audio, LogSweepAtUnitPeakHasSineRms)
{
    const auto sweep = eeq::dsp::LogSweepGenerator::generate (48000.0, 4.0, 20.0, 20000.0, 1.0f);
    ASSERT_FALSE (sweep.empty());

    float peak = 0.0f;

    for (const auto sample : sweep)
        peak = std::max (peak, std::abs (sample));

    EXPECT_NEAR (peak, 1.0f, 0.01f);
    EXPECT_NEAR (rmsOf (sweep, 0), 0.70710678, 0.01);
}

// issue #51: 全励起ソースの実効RMSが「設定レベル」に揃うこと。
// 生成/波形のRMS × rmsNormaliseFor(ソース) = 1.0 が定義
TEST (Audio, RmsNormaliseMatchesEachSourceRms)
{
    using S = eeq::audio::AudioEngine::ExcitationSource;

    // ピンクノイズ: 生成器が実効RMS 1.0
    EXPECT_NEAR (eeq::audio::AudioEngine::rmsNormaliseFor (S::pinkNoise), 1.0f, 1e-6);

    // スイープ: ピーク1.0 → 実効RMS 0.7071 → 係数 √2 で RMS 1.0
    EXPECT_NEAR (0.70710678 * eeq::audio::AudioEngine::rmsNormaliseFor (S::sweep), 1.0f, 1e-4);

    // 1kHzトーン: 正弦波の実効RMS 0.7071 → 係数 √2
    EXPECT_NEAR (0.70710678 * eeq::audio::AudioEngine::rmsNormaliseFor (S::tone1k), 1.0f, 1e-4);

    // 1kHz帯域ノイズ: 実装と同じフィルタを通した実測RMS（約0.314）と係数の整合
    eeq::audio::PinkNoiseGenerator generator;
    const auto sampleRate = 48000.0;
    const auto w0 = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
    const auto alpha = std::sin (w0) / (2.0 * 1.414);
    const auto b0 = alpha / (1.0 + alpha);
    const auto b2 = -alpha / (1.0 + alpha);
    const auto a1 = -2.0 * std::cos (w0) / (1.0 + alpha);
    const auto a2 = (1.0 - alpha) / (1.0 + alpha);

    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    double sumSquares = 0.0;
    constexpr int count = 1 << 20;

    for (int i = 0; i < count; ++i)
    {
        const auto pink = generator.nextSample();
        const auto out = b0 * pink + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = pink; y2 = y1; y1 = out;

        if (i > 4096) // 立ち上がりを捨てる
            sumSquares += out * out;
    }

    const auto measuredRms = std::sqrt (sumSquares / (double) (count - 4096));
    EXPECT_NEAR (measuredRms * eeq::audio::AudioEngine::rmsNormaliseFor (S::bandNoise1k), 1.0, 0.2);
}

// issue #52: スライダー上限の根拠。ユニットRMSピンクのクレストは 12〜16 dB に収まる
TEST (Audio, PinkNoiseCrestIsInExpectedRange)
{
    eeq::audio::PinkNoiseGenerator generator;
    constexpr int count = 1 << 20;
    double sumSquares = 0.0;
    float peak = 0.0f;

    for (int i = 0; i < count; ++i)
    {
        const auto sample = generator.nextSample();
        sumSquares += (double) sample * (double) sample;
        peak = std::max (peak, std::abs (sample));
    }

    const auto rms = std::sqrt (sumSquares / (double) count);
    const auto crestDb = 20.0 * std::log10 ((double) peak / rms);
    EXPECT_GE (crestDb, 12.0);
    EXPECT_LE (crestDb, 16.0);
}

// issue #53: フェードイン中に停止しても包絡が連続する（フルレベルへ跳ねない）
TEST (Audio, StoppingDuringFadeInKeepsEnvelopeContinuous)
{
    using eeq::audio::AudioEngine;
    constexpr int fadeInLength = 300;
    constexpr int fadeOutLength = 150;

    // フェードイン 20% の位置で停止 → その包絡からフェードアウトが始まる
    const auto atStop = AudioEngine::fadeEnvelopeAt (1, (int) (0.2 * fadeInLength), fadeInLength, 0.0f);
    EXPECT_NEAR (atStop, 0.2f, 1e-4);

    const auto fadeOutStart = AudioEngine::fadeEnvelopeAt (3, 0, fadeOutLength, atStop);
    EXPECT_NEAR (fadeOutStart, atStop, 1e-4); // 不連続ゼロ

    // フェードアウト中に再開した場合も現在値からフェードインが続く
    const auto midFadeOut = AudioEngine::fadeEnvelopeAt (3, fadeOutLength / 2, fadeOutLength, atStop);
    const auto resumed = AudioEngine::fadeEnvelopeAt (1, 0, fadeInLength, midFadeOut);
    EXPECT_NEAR (resumed, midFadeOut, 1e-4);

    // 定常からの停止は 1.0 からフェードアウト（従来どおり）
    EXPECT_NEAR (AudioEngine::fadeEnvelopeAt (3, 0, fadeOutLength, 1.0f), 1.0f, 1e-4);
}

TEST (Audio, UiWatchdogReservesFadeAndOneAudioBlockWithin500Ms)
{
    using eeq::audio::AudioEngine;

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto budget = (std::uint64_t) (sampleRate * AudioEngine::uiWatchdogSilenceBudgetSeconds);
        const auto fade = (std::uint64_t) (sampleRate * AudioEngine::fadeOutSeconds);

        for (const auto bufferSize : { 64, 512, 2048 })
        {
            const auto timeout = AudioEngine::uiWatchdogTimeoutSamplesFor (sampleRate, bufferSize);
            EXPECT_GT (timeout, 0u);
            EXPECT_LE (timeout + fade + (std::uint64_t) bufferSize, budget)
                << "sampleRate=" << sampleRate << " bufferSize=" << bufferSize;
        }
    }
}

} // namespace
