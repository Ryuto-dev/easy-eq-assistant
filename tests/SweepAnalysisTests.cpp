#include <gtest/gtest.h>

#include "DSP/SweepAnalysis.h"
#include "DSP/SweepGenerator.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

// スイープ測定（2026-11）: 鳴らし終わってから全体を割る方式で、
//  - ゲイン・遅延が正しく出る、コヒーレンス（S/N から）が帯域全体で高い
//  - 指数減衰の残響（RT = 0.8 s）の T20 / T30 が ±10 % に入る
namespace
{
struct Room
{
    std::vector<float> ref, meas;
};

// 直接音（遅延 480 サンプル、ゲイン 0.5）＋ RT 0.8 s の拡散残響（指数減衰する白色雑音）＋ 背景雑音
Room makeRoom (double fs, double sweepSeconds, double rt, double noiseLevel, float reverbAmplitude = 0.02f)
{
    const auto sweep = eeq::dsp::LogSweepGenerator::generate (fs, sweepSeconds, 20.0, 20000.0, 1.0f);
    const auto tail = (int) (fs * 2.0);
    const auto pre = (int) (fs * 0.3);
    const auto length = pre + (int) sweep.size() + tail;

    Room room;
    room.ref.assign ((size_t) length, 0.0f);
    std::copy (sweep.begin(), sweep.end(), room.ref.begin() + pre);

    // 部屋の IR
    std::mt19937 rng (7);
    std::normal_distribution<float> gauss (0.0f, 1.0f);
    const auto irLength = (int) (fs * 1.5);
    std::vector<float> ir ((size_t) irLength, 0.0f);
    ir[480] = 0.5f;
    const auto decay = std::log (1000.0) / rt; // 振幅で 60 dB / rt
    for (int i = 480 + (int) (fs * 0.005); i < irLength; ++i)
        ir[(size_t) i] += reverbAmplitude * gauss (rng) * (float) std::exp (-decay * (double) (i - 480) / fs);

    // 畳み込み（FFT で）
    const auto n = eeq::dsp::sweepdetail::nextPow2 (length + irLength);
    auto X = eeq::dsp::sweepdetail::forward (room.ref, n);
    const auto Hs = eeq::dsp::sweepdetail::forward (ir, n);
    for (size_t k = 0; k < X.size(); ++k)
        X[k] *= Hs[k];
    auto y = eeq::dsp::sweepdetail::inverse (X, n);

    room.meas.assign ((size_t) length, 0.0f);
    for (int i = 0; i < length; ++i)
        room.meas[(size_t) i] = y[(size_t) i] + noiseLevel * gauss (rng);

    return room;
}
}

TEST (SweepAnalysis, RecoversGainDelayAndHighCoherence)
{
    const double fs = 48000.0;
    const auto room = makeRoom (fs, 5.0, 0.8, 1.0e-4f, 0.002f); // 残響は直接音より 15 dB ほど小さい

    eeq::dsp::SweepImpulse impulse;
    ASSERT_TRUE (eeq::dsp::deconvolveSweep (room.ref, room.meas, fs, impulse));
    EXPECT_EQ (impulse.peakIndex, 480);

    // 時間合わせ 480 サンプルでフレームにすると、直接音が t = 0 に来る
    eeq::dsp::SpectrumFrame frame;
    eeq::dsp::frameFromSweep (impulse, 16384, 480, frame);
    ASSERT_EQ (frame.fftSize, 16384);
    ASSERT_EQ (frame.transfer.size(), 8193u);
    EXPECT_TRUE (frame.hasReliableCoherence());

    // 1/4 oct 程度で平均したレベルは直接音の −6 dB（残響ぶん +0.2 dB）
    const auto binHz = fs / frame.fftSize;
    for (const double f : { 100.0, 1000.0, 8000.0 })
    {
        double power = 0.0, coherence = 0.0;
        int count = 0;
        for (size_t k = 1; k < frame.magnitudeDb.size(); ++k)
        {
            const auto fk = (double) k * binHz;
            if (fk < f / 1.12 || fk > f * 1.12)
                continue;
            power += std::pow (10.0, frame.magnitudeDb[k] / 10.0);
            coherence += frame.coherence[k];
            ++count;
        }
        const auto levelDb = 10.0 * std::log10 (power / count);
        EXPECT_NEAR (levelDb, -5.8, 1.0) << f << " Hz";
        EXPECT_GT (coherence / count, 0.9) << f << " Hz";
    }
}

TEST (SweepAnalysis, ReverbTimeOfExponentialDecay)
{
    const double fs = 48000.0;
    const double rt = 0.8;
    const auto room = makeRoom (fs, 5.0, rt, 1.0e-5f);

    eeq::dsp::SweepImpulse impulse;
    ASSERT_TRUE (eeq::dsp::deconvolveSweep (room.ref, room.meas, fs, impulse));
    const auto reverb = eeq::dsp::analyseReverb (impulse);
    ASSERT_GE (reverb.bands.size(), 8u);

    for (const auto& band : reverb.bands)
    {
        if (band.centreHz > 0.0 && band.centreHz < 250.0)
            continue; // 低域は 1.5 秒の IR では帯域フィルタの立ち上がりが長いので判定しない
        SCOPED_TRACE (band.centreHz);
        EXPECT_GT (band.dynamicRangeDb, 45.0);
        EXPECT_NEAR (band.t20, rt, rt * 0.1);
        EXPECT_NEAR (band.t30, rt, rt * 0.1);
    }
}

TEST (SweepAnalysis, NoisyRoomGivesNoT30)
{
    const double fs = 48000.0;
    const auto room = makeRoom (fs, 3.0, 0.6, 0.3f); // 背景雑音が大きい（スイープは長いほど雑音に強いので、かなり大きくする）

    eeq::dsp::SweepImpulse impulse;
    ASSERT_TRUE (eeq::dsp::deconvolveSweep (room.ref, room.meas, fs, impulse));
    const auto reverb = eeq::dsp::analyseReverb (impulse);
    const auto& broadband = reverb.bands.back();
    EXPECT_LT (broadband.dynamicRangeDb, 45.0);
    EXPECT_EQ (broadband.t30, 0.0); // S/N が足りないときは出さない
}

// 10 秒スイープの解析が実用的な時間で終わる（鳴り終わって 2 秒待ったあと、すぐ結果を出したい）
TEST (SweepAnalysis, TenSecondSweepAnalysesQuickly)
{
    const double fs = 48000.0;
    const auto sweep = eeq::dsp::LogSweepGenerator::generate (fs, 10.0, 20.0, 20000.0, 1.0f);
    std::vector<float> ref (sweep.size() + (size_t) (fs * 2.3), 0.0f), meas (ref.size(), 0.0f);
    std::copy (sweep.begin(), sweep.end(), ref.begin() + 14400);
    for (size_t i = 480; i < ref.size(); ++i)
        meas[i] = 0.5f * ref[i - 480];

    const auto t0 = std::chrono::steady_clock::now();
    eeq::dsp::SweepImpulse impulse;
    ASSERT_TRUE (eeq::dsp::deconvolveSweep (ref, meas, fs, impulse));
    const auto t1 = std::chrono::steady_clock::now();
    eeq::dsp::SpectrumFrame frame;
    eeq::dsp::frameFromSweep (impulse, 16384, 0, frame);
    const auto reverb = eeq::dsp::analyseReverb (impulse);
    const auto t2 = std::chrono::steady_clock::now();

    const auto ms = [] (auto a, auto b) { return std::chrono::duration<double, std::milli> (b - a).count(); };
    std::printf ("sweep analysis: n=%zu deconvolve %.0f ms, frame+reverb %.0f ms\n", impulse.ir.size(), ms (t0, t1), ms (t1, t2));
    EXPECT_LT (ms (t0, t2), 3000.0);
}
