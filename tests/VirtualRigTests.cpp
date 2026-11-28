#include <gtest/gtest.h>

#include "VirtualRig.h"

#include "DSP/MeasurementEngine.h"
#include "DSP/RtaSplAnalyzer.h"

#include <cmath>
#include <cstdio>

// ロードマップ A-1: 仮想デバイスでアプリの測定経路を丸ごと通す。
//   AudioEngine（ピンクノイズ・内部参照）→ 既知の系 → 入力 → リングバッファ → MeasurementEngine → 結果
// 部品単体ではなく「つないだときに数字が正しいか」を固定する。
namespace
{
using eeq::test::KnownSystem;
using eeq::test::VirtualRig;

struct Comparison
{
    double maxMagErrorDb = 0.0, maxPhaseErrorDeg = 0.0, minCoherence = 1.0;
};

// 100 Hz〜10 kHz の bin を 1/3 oct ごとにまとめて（雑音で 1 bin が暴れるのを避ける）真値と比べる
Comparison compare (const eeq::dsp::SpectrumFrame& frame, const KnownSystem& system, double rate, double residualDelaySamples)
{
    Comparison c;
    const auto binHz = frame.sampleRate / frame.fftSize;

    for (double centre = 100.0; centre <= 10000.0; centre *= std::pow (2.0, 1.0 / 3.0))
    {
        std::complex<double> measured = 0.0;
        double coherence = 0.0;
        int count = 0;
        for (size_t k = 1; k < frame.transfer.size(); ++k)
        {
            const auto f = (double) k * binHz;
            if (f < centre / 1.12 || f > centre * 1.12)
                continue;
            // 残っている遅延ぶんの位相を戻してから平均する
            const auto undo = std::polar (1.0, 2.0 * juce::MathConstants<double>::pi * f * residualDelaySamples / rate);
            measured += std::complex<double> (frame.transfer[k]) * undo / system.response (f, rate);
            coherence += frame.coherence[k];
            ++count;
        }
        if (count == 0)
            continue;
        measured /= (double) count;
        c.maxMagErrorDb = std::max (c.maxMagErrorDb, std::abs (20.0 * std::log10 (std::abs (measured))));
        c.maxPhaseErrorDeg = std::max (c.maxPhaseErrorDeg, std::abs (std::arg (measured) * 180.0 / juce::MathConstants<double>::pi));
        c.minCoherence = std::min (c.minCoherence, coherence / count);
    }
    return c;
}

KnownSystem roomWith (int extraDelay)
{
    KnownSystem s;
    s.extraDelaySamples = extraDelay;
    s.gainDb = -12.0;
    eeq::dsp::peq::Band bell;
    bell.frequency = 1000.0;
    bell.gainDb = 6.0;
    bell.q = 4.0;
    eeq::dsp::peq::Band shelf;
    shelf.type = eeq::dsp::peq::Band::Type::highShelf;
    shelf.frequency = 6000.0;
    shelf.gainDb = -4.0;
    s.filters = { bell, shelf };
    s.noiseRms = 1.0e-4; // −80 dBFS
    return s;
}

void startNoiseAndMeasure (VirtualRig& rig, int fftSize, bool alignDelay)
{
    auto& engine = rig.getEngine();
    engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
    engine.setNoiseLevelDb (-20.0f);

    auto& measurement = engine.getMeasurement();
    eeq::dsp::AveragingConfig averaging;
    averaging.mode = eeq::dsp::AveragingConfig::Mode::fifo;
    averaging.maxAverages = 16;
    measurement.configure (fftSize, rig.getSampleRate(), averaging, juce::dsp::WindowingFunction<float>::hann);
    measurement.setReferenceDelaySamples (alignDelay ? rig.totalDelaySamples() : 0);

    engine.startExcitation();
    rig.run (0.5); // フェードイン
    measurement.startMeasurement();
}
}

// 遅延 0 / 40 / 120 ms、全体 −12 dB ＋ PEQ の系で、振幅・位相・γ² が真値に一致する。
// 遅延の検出（GCC-PHAT）も実際の遅延を 1 サンプル以内で当てる
TEST (VirtualRig, TransferFunctionMatchesKnownSystemAtSeveralDelays)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double rate = 48000.0;

    for (const int extraMs : { 0, 40, 120 })
    {
        SCOPED_TRACE (extraMs);
        VirtualRig rig (rate, 256, roomWith ((int) std::lround (extraMs * rate / 1000.0)));
        startNoiseAndMeasure (rig, 16384, true);
        rig.run (5.0);

        auto& measurement = rig.getEngine().getMeasurement();
        const auto estimate = measurement.estimateCurrentDelay (true);
        ASSERT_TRUE (estimate.valid);
        EXPECT_NEAR (estimate.delaySamples, rig.totalDelaySamples(), 1.0);

        const auto frame = measurement.getLatestFrame();
        ASSERT_NE (frame, nullptr);
        ASSERT_GE (frame->averages, 8);
        const auto c = compare (*frame, rig.getSystem(), rate, rig.totalDelaySamples() - frame->refDelaySamples);
        std::printf ("delay %d ms: |H| error %.2f dB, phase error %.1f deg, min coherence %.3f\n",
                     extraMs, c.maxMagErrorDb, c.maxPhaseErrorDeg, c.minCoherence);
        EXPECT_LT (c.maxMagErrorDb, 0.5);
        EXPECT_LT (c.maxPhaseErrorDeg, 5.0);
        EXPECT_GT (c.minCoherence, 0.95);

        // ハーネス自体の確認: 真値を 1 dB ずらすと、ちゃんと 1 dB の誤差として出る（比較が甘くないこと）
        auto wrong = rig.getSystem();
        wrong.gainDb += 1.0;
        EXPECT_NEAR (compare (*frame, wrong, rate, rig.totalDelaySamples() - frame->refDelaySamples).maxMagErrorDb, 1.0, 0.1);

        // MTW（短い窓）も、遅延を合わせていれば同じ真値に乗る
        const auto mtw = measurement.getLatestMtwFrame();
        ASSERT_NE (mtw, nullptr);
        const auto cm = compare (*mtw, rig.getSystem(), rate, rig.totalDelaySamples() - mtw->refDelaySamples);
        EXPECT_LT (cm.maxMagErrorDb, 0.7);
        EXPECT_GT (cm.minCoherence, 0.9);
    }
}

// 遅延を合わせていなくても振幅は正しい（位相は遅延ぶん回るだけ）。#106 の回帰
TEST (VirtualRig, UnalignedDelayKeepsMagnitude)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double rate = 48000.0;
    VirtualRig rig (rate, 256, roomWith ((int) std::lround (0.010 * rate))); // 10 ms
    startNoiseAndMeasure (rig, 32768, false);
    rig.run (8.0);

    const auto frame = rig.getEngine().getMeasurement().getLatestFrame();
    ASSERT_NE (frame, nullptr);
    const auto c = compare (*frame, rig.getSystem(), rate, rig.totalDelaySamples() - frame->refDelaySamples);
    EXPECT_LT (c.maxMagErrorDb, 0.8);
    EXPECT_LT (c.maxPhaseErrorDeg, 8.0);
}

// 入出力のクロックが 100 ppm ずれていても、ずれを推定して補正し、γ² が落ちない（B-6）
TEST (VirtualRig, ClockDriftIsEstimatedAndCompensated)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double rate = 48000.0;
    auto system = roomWith ((int) std::lround (0.020 * rate));
    system.clockDriftPpm = 100.0;
    VirtualRig rig (rate, 256, system);
    startNoiseAndMeasure (rig, 16384, true);
    rig.run (20.0);

    auto& measurement = rig.getEngine().getMeasurement();
    std::printf ("drift: estimated %.1f ppm, compensating %.1f ppm\n",
                 measurement.getEstimatedDriftPpm(), measurement.getCompensatedDriftPpm());
    // 符号は「入力側が速い = 遅延が縮む」をマイナスで表す規約
    EXPECT_NEAR (std::abs (measurement.getEstimatedDriftPpm()), 100.0, 10.0);
    EXPECT_NEAR (std::abs (measurement.getCompensatedDriftPpm()), 100.0, 10.0);

    const auto frame = measurement.getLatestFrame();
    ASSERT_NE (frame, nullptr);
    double coherence = 0.0;
    int count = 0;
    const auto binHz = frame->sampleRate / frame->fftSize;
    for (size_t k = 1; k < frame->coherence.size(); ++k)
        if (const auto f = (double) k * binHz; f > 200.0 && f < 8000.0)
        {
            coherence += frame->coherence[k];
            ++count;
        }
    EXPECT_GT (coherence / count, 0.9);
}

// サンプルレートを変えても、同じ系・同じ信号なら SPL と伝達関数の値が一致する（#113）
TEST (VirtualRig, SampleRateChangeKeepsSplAndTransferFunction)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    double spl[2] = {}, gainAt1k[2] = {};
    const double rates[] = { 44100.0, 96000.0 };

    for (int r = 0; r < 2; ++r)
    {
        const auto rate = rates[r];
        VirtualRig rig (rate, 512, roomWith ((int) std::lround (0.005 * rate)));
        auto& analyzer = rig.getEngine().getSplAnalyzer();
        analyzer.configure (rate, 3, 1.0);
        analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::zWeighted);
        startNoiseAndMeasure (rig, rate > 50000.0 ? 32768 : 16384, true);
        analyzer.resetSpl();
        rig.run (6.0);

        eeq::dsp::RtaSplAnalyzer::SplSnapshot snapshot;
        analyzer.copySpl (snapshot);
        spl[r] = snapshot.leq1minDb;

        const auto frame = rig.getEngine().getMeasurement().getLatestFrame();
        ASSERT_NE (frame, nullptr);
        const auto binHz = frame->sampleRate / frame->fftSize;
        double power = 0.0;
        int count = 0;
        for (size_t k = 1; k < frame->magnitudeDb.size(); ++k)
            if (const auto f = (double) k * binHz; f > 800.0 && f < 1250.0)
            {
                power += std::pow (10.0, frame->magnitudeDb[k] / 10.0);
                ++count;
            }
        gainAt1k[r] = 10.0 * std::log10 (power / count);
        std::printf ("%.0f Hz: Leq %.2f dB, |H| around 1 kHz %.2f dB\n", rate, spl[r], gainAt1k[r]);
    }

    // 同じピンクノイズ（-20 dBFS RMS）は、帯域の上限が変わってもほぼ同じエネルギー（20 kHz 超の分は小さい）
    EXPECT_NEAR (spl[0], spl[1], 0.5);
    EXPECT_NEAR (gainAt1k[0], gainAt1k[1], 0.3);
}
