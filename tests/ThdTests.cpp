#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "DSP/ThdAnalyzer.h"

namespace
{

constexpr double sampleRate = 48000.0;

std::vector<float> makeSweep (double seconds)
{
    return eeq::dsp::LogSweepGenerator::generate (sampleRate, seconds, 20.0, 20000.0, 1.0f);
}

/** スイープを lap サンプル遅延で非線形 f を通して「録音」を作る。
    実機の録音と同じく、録音長 = スイープ + 1.2秒の残響 tail */
std::vector<float> makeRecording (const std::vector<float>& sweep, int lag,
                                  const std::function<float (float)>& f)
{
    const auto tailSamples = (size_t) std::llround (1.2 * sampleRate);
    std::vector<float> recorded (sweep.size() + tailSamples, 0.0f);

    for (size_t i = 0; i < recorded.size(); ++i)
    {
        const auto source = (long long) i - (long long) lag;

        if (source >= 0 && source < (long long) sweep.size())
            recorded[i] = f (sweep[(size_t) source]);
    }

    return recorded;
}

} // namespace

// 線形系（歪みなし）では THD がほぼ 0 になり、issue #30 の「常に 0.00%」とは
// 「真のゼロを正しく検出する」意味でだけ一致する
TEST (Thd, LinearSystemYieldsNearZeroThd)
{
    const auto sweep = makeSweep (2.0);
    const auto recorded = makeRecording (sweep, 1000, [] (float v) { return v; });

    const auto result = eeq::dsp::analyzeThdFromSweepRecording (sweep, recorded, sampleRate, 2.0, 20.0, 20000.0);

    ASSERT_TRUE (result.valid) << result.failureReason;
    EXPECT_LT (result.thdPercent, 0.5);
    EXPECT_NEAR (result.linearLagSamples, 1000.0, 20.0);
}

// 既知の非線形（3次のみ）では 3次高調波が2次を上回り、THD が正の値になる。
// 旧実装はこの場合も常に 0.00% を表示していた（issue #30 の退行テスト）
TEST (Thd, CubicDistortionYieldsPositiveThdWithOddHarmonics)
{
    const auto sweep = makeSweep (2.0);
    const auto recorded = makeRecording (sweep, 1000, [] (float v)
    {
        const auto x = (double) v;
        return (float) (x + 0.02 * x * x * x);
    });

    const auto result = eeq::dsp::analyzeThdFromSweepRecording (sweep, recorded, sampleRate, 2.0, 20.0, 20000.0);

    ASSERT_TRUE (result.valid) << result.failureReason;
    EXPECT_GT (result.thdPercent, 0.1);
    EXPECT_LT (result.thdPercent, 2.0);
    EXPECT_GT (result.harmonicPercent[1], result.harmonicPercent[0]); // 3次 > 2次
}

// tanh 系のソフトクリップでも THD が有意に検出される
TEST (Thd, SoftClipYieldsDetectableThd)
{
    const auto sweep = makeSweep (2.0);
    const auto recorded = makeRecording (sweep, 1000, [] (float v)
    {
        return (float) (std::tanh (3.0 * (double) v) / std::tanh (3.0));
    });

    const auto result = eeq::dsp::analyzeThdFromSweepRecording (sweep, recorded, sampleRate, 2.0, 20.0, 20000.0);

    ASSERT_TRUE (result.valid) << result.failureReason;
    EXPECT_GT (result.thdPercent, 5.0);
    EXPECT_LT (result.thdPercent, 60.0);
    EXPECT_GT (result.harmonicPercent[1], result.harmonicPercent[0]); // 奇次優勢
}

// 録音が短すぎる場合は「測定不能」と理由を返す（0.00% と誤表示しない）
TEST (Thd, ShortRecordingIsReportedAsFailure)
{
    const auto sweep = makeSweep (2.0);
    const std::vector<float> half ((size_t) sweep.size() / 2, 0.0f);

    const auto result = eeq::dsp::analyzeThdFromSweepRecording (sweep, half, sampleRate, 2.0, 20.0, 20000.0);

    EXPECT_FALSE (result.valid);
    EXPECT_FALSE (result.failureReason.isEmpty());
}

namespace
{
const eeq::dsp::ThdBand* bandAt (const eeq::dsp::ThdResult& result, double nominalHz)
{
    for (const auto& band : result.bands)
        if (std::abs (band.nominalHz - nominalHz) < 1.0e-6)
            return &band;

    return nullptr;
}

// RBJ ローパス（双二次、直接形 I）
struct LowPass
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    LowPass (double fc, double q)
    {
        const auto w0 = 2.0 * juce::MathConstants<double>::pi * fc / sampleRate;
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        b0 = (1.0 - std::cos (w0)) / 2.0 / a0;
        b1 = (1.0 - std::cos (w0)) / a0;
        b2 = b0;
        a1 = -2.0 * std::cos (w0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process (double x)
    {
        const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};
} // namespace

// #114: 250 Hz 未満にだけ 10% の2次歪みがある系（4次 Butterworth LP を通した成分の2乗）。
// 周波数ごとの THD は 10%/(1+(f/250)^8) になる。エネルギー集約していた旧実装は
// 約 1.9% と表示していた
TEST (Thd, LowFrequencyOnlyDistortionIsResolvedPerBand)
{
    constexpr double seconds = 10.0; // LimitTestTab と同じ
    const auto sweep = eeq::dsp::LogSweepGenerator::generate (sampleRate, seconds, 20.0, 20000.0, 1.0f);

    LowPass stage1 (250.0, 0.54119610), stage2 (250.0, 1.30656296);
    const auto recorded = makeRecording (sweep, 1000, [&] (float v)
    {
        const auto low = stage2.process (stage1.process ((double) v));
        return (float) ((double) v + 0.2 * low * low);
    });

    const auto result = eeq::dsp::analyzeThdFromSweepRecording (sweep, recorded, sampleRate, seconds, 20.0, 20000.0);

    ASSERT_TRUE (result.valid) << result.failureReason;

    auto expectedPercent = [] (double f) { return 10.0 / (1.0 + std::pow (f / 250.0, 8.0)); };

    for (const auto nominalHz : { 40.0, 63.0, 100.0, 160.0 })
    {
        const auto* band = bandAt (result, nominalHz);
        ASSERT_NE (band, nullptr) << nominalHz;
        EXPECT_NEAR (band->thdPercent, expectedPercent (band->centerHz), 0.5) << nominalHz;
        EXPECT_NEAR (band->harmonicPercent[0], band->thdPercent, 0.05) << nominalHz; // 2次だけ
    }

    for (const auto nominalHz : { 1000.0, 4000.0 })
    {
        const auto* band = bandAt (result, nominalHz);
        ASSERT_NE (band, nullptr) << nominalHz;
        EXPECT_LT (band->thdPercent, 0.1) << nominalHz;
    }

    // 代表値は最悪バンド（低域）の値: 約 10%
    EXPECT_NEAR (result.thdPercent, 10.0, 0.5);
    EXPECT_LE (result.worstFrequencyHz, 160.0);
    EXPECT_NEAR (result.harmonicPercent[0], result.thdPercent, 0.05);
}

// #114: 逆フィルタの正則化 ε = 1e-3·max|X|² は 20 kHz で −6 dB 減衰させるため、
// 補正しないと高域の高調波が過小評価される（6.3 kHz で約 4%）。周波数に依らない
// 5% の2次歪みが 100 Hz でも 6.3 kHz（2次は 12.6 kHz）でも 5% と出る
TEST (Thd, RegularisationLossIsCompensatedAtHighFrequency)
{
    const auto sweep = makeSweep (2.0);
    const auto recorded = makeRecording (sweep, 1000, [] (float v)
    {
        const auto x = (double) v;
        return (float) (x + 0.1 * x * x);
    });

    const auto result = eeq::dsp::analyzeThdFromSweepRecording (sweep, recorded, sampleRate, 2.0, 20.0, 20000.0);

    ASSERT_TRUE (result.valid) << result.failureReason;

    for (const auto nominalHz : { 100.0, 1000.0, 4000.0, 6300.0 })
    {
        const auto* band = bandAt (result, nominalHz);
        ASSERT_NE (band, nullptr) << nominalHz;
        EXPECT_NEAR (band->thdPercent, 5.0, 0.25) << nominalHz;
    }

    EXPECT_NEAR (result.thdPercent, 5.0, 0.3);
}
