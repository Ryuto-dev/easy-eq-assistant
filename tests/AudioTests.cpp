#include <gtest/gtest.h>

#include <cmath>
#include <vector>

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

// スイープはピーク1.0で生成され、オーディオスレッドが 1/√2 を掛けて実効RMS基準に揃える。
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

} // namespace
