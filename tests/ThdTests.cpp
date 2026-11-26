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
