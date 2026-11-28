#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "DSP/ClockDriftCompensator.h"
#include "DSP/DelayEstimator.h"

// 入出力のクロック差（ドリフト）の補正（ClockDriftCompensator）。
// MacBook のスピーカー → BMG-22M で実測した約 83 ppm を合成信号で再現する。
namespace
{
using eeq::dsp::ClockDriftCompensator;

// x を時刻 t（小数サンプル）で帯域制限補間する（試験信号を作る側。補正器とは独立な長い sinc）
double sampleAt (const std::vector<float>& x, double t)
{
    const auto pi = 3.14159265358979323846;
    const int half = 48;
    const auto centre = (int) std::floor (t);
    double acc = 0.0;

    for (int k = centre - half + 1; k <= centre + half; ++k)
    {
        if (k < 0 || k >= (int) x.size())
            continue;

        const auto d = t - (double) k;
        const auto sinc = std::abs (d) < 1e-12 ? 1.0 : std::sin (pi * d) / (pi * d);
        const auto window = 0.5 + 0.5 * std::cos (pi * d / (double) half);
        acc += (double) x[(size_t) k] * sinc * window;
    }

    return acc;
}

struct DriftRun
{
    std::vector<float> refOut, measOut;
    std::vector<bool> activeAt;
    ClockDriftCompensator compensator { (1 << 18) };
};

// 長さ seconds の白色雑音 Ref と、遅延 delay0 + ppm·t で遅れる Meas を流す
void runDrift (DriftRun& run, double ppm, double delay0, double seconds, int baseDelayFromSecond, int baseDelay)
{
    constexpr double rate = 48000.0;
    const auto total = (int) (rate * seconds);
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.2f);

    std::vector<float> ref ((size_t) total);
    for (auto& v : ref)
        v = noise (rng);

    std::vector<float> meas ((size_t) total);
    for (int n = 0; n < total; ++n)
    {
        const auto d = delay0 + ppm * 1.0e-6 * (double) n;
        meas[(size_t) n] = (float) sampleAt (ref, (double) n - d);
    }

    run.compensator.setMinimumSpan (rate * 3.0);
    run.refOut.assign ((size_t) total, 0.0f);
    run.measOut.assign ((size_t) total, 0.0f);
    run.activeAt.assign ((size_t) total, false);

    constexpr int block = 512;
    constexpr int window = 32768;
    std::uint64_t nextObservation = 0;

    for (int start = 0; start + block <= total; start += block)
    {
        const auto base = start >= (int) (baseDelayFromSecond * rate) ? baseDelay : 0;

        if (start >= (int) (baseDelayFromSecond * rate) && start - block < (int) (baseDelayFromSecond * rate))
            run.compensator.rebase(); // 基準遅延を設定した瞬間（MeasurementEngine は遅延が変わったブロックで呼ぶ）

        run.compensator.process (&ref[(size_t) start], &meas[(size_t) start], &run.refOut[(size_t) start],
                                 &run.measOut[(size_t) start], block, base);
        run.compensator.consumeAlignmentChanged();

        for (int i = 0; i < block; ++i)
            run.activeAt[(size_t) (start + i)] = run.compensator.isActive();

        const auto now = run.compensator.getSamplesProcessed();

        if (now >= nextObservation && (int) now >= window)
        {
            nextObservation = now + (std::uint64_t) (rate * 0.5);
            const auto e = eeq::dsp::estimateDelay (&ref[(size_t) now - window], &meas[(size_t) now - window], window,
                                                    true, 1.0);
            if (e.confidence >= 15.0)
                run.compensator.addObservation ((double) now - window * 0.5, e.delaySamples);
        }
    }
}

// 補正後の Ref/Meas の残りの時間差（16384 サンプル窓）
double residualAt (const DriftRun& run, double second)
{
    const auto end = (int) (second * 48000.0);
    const auto e = eeq::dsp::estimateDelay (&run.refOut[(size_t) end - 16384], &run.measOut[(size_t) end - 16384],
                                            16384, false, 1.0);
    return e.delaySamples;
}
} // namespace

TEST (ClockDrift, TracksEightyThreePpmAndKeepsRefAlignedToMeas)
{
    // 83 ppm、初期遅延 2000 サンプル。2 秒目から「遅延を検出して適用した」状態（基準遅延 = 当時の実遅延）
    DriftRun run;
    const auto delayAt2s = 2000.0 + 83.0e-6 * 2.0 * 48000.0;
    runDrift (run, 83.0, 2000.0, 14.0, 2, (int) std::lround (delayAt2s));

    EXPECT_NEAR (run.compensator.getDriftPpm(), 83.0, 1.0);
    EXPECT_TRUE (run.compensator.isActive());

    if (std::getenv ("EEQ_DRIFT_DEBUG") != nullptr)
        for (double t : { 1.0, 2.2, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 6.0, 8.0 })
            std::printf ("t=%.1f residual=%.3f\n", t, residualAt (run, t));
    if (std::getenv ("EEQ_DRIFT_DEBUG") != nullptr)
        std::printf ("offset(end)=%.3f predict(2s)=%.3f predict(end)=%.3f true(2s)=%.3f true(end)=%.3f\n",
                     run.compensator.getOffsetForTest(), run.compensator.predictForTest (96000.0),
                     run.compensator.predictForTest (14.0 * 48000.0), 2000.0 + 83e-6 * 96000.0, 2000.0 + 83e-6 * 14.0 * 48000.0);

    // 補正が始まるのは、観測が溜まって（3 秒分）から 1 秒以内
    int firstActive = -1;
    for (size_t i = 0; i < run.activeAt.size(); ++i)
        if (run.activeAt[i]) { firstActive = (int) i; break; }

    ASSERT_GE (firstActive, 0);
    EXPECT_LT (firstActive, (int) (5.0 * 48000.0)) << "compensation engaged at " << firstActive / 48000.0 << " s";

    // 補正中は Ref と Meas の時間差が残らない（丸めた基準遅延との差 ±0.5 以内＋ドリフトの追従誤差）
    for (double t : { 7.0, 10.0, 13.5 })
        EXPECT_NEAR (residualAt (run, t), 0.0, 0.6) << "at " << t << " s";

    // 時間差が動かない（ドリフトを打ち消している）: 7 秒→13.5 秒で 0.2 サンプル未満の変化
    EXPECT_NEAR (residualAt (run, 13.5) - residualAt (run, 7.0), 0.0, 0.2);
}

TEST (ClockDrift, SameClockLeavesTheIntegerDelayLineUntouched)
{
    DriftRun run;
    runDrift (run, 0.0, 1500.0, 8.0, 2, 1500);

    EXPECT_FALSE (run.compensator.isActive());
    EXPECT_NEAR (run.compensator.getDriftPpm(), 0.0, 0.5);

    // 補正していないので Meas はそのまま、Ref は整数 1500 サンプル遅れ
    EXPECT_NEAR (residualAt (run, 7.5), 0.0, 0.05);
}

TEST (ClockDrift, NegativeDriftIsCompensatedToo)
{
    DriftRun run;
    const auto delayAt2s = 3000.0 - 60.0e-6 * 2.0 * 48000.0;
    runDrift (run, -60.0, 3000.0, 12.0, 2, (int) std::lround (delayAt2s));

    EXPECT_NEAR (run.compensator.getDriftPpm(), -60.0, 1.0);
    EXPECT_TRUE (run.compensator.isActive());
    EXPECT_NEAR (residualAt (run, 11.5) - residualAt (run, 7.0), 0.0, 0.2);
}
