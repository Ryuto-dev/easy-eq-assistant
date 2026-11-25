// 総合検証（仕様 7.2「意図的な変化の復元テスト」をソフト内で再現するテスト）。
//   1. 既知のPEQ（1 kHz +6 dB、Q=4 など）を通した信号を作る（= 意図的な変化）
//   2. デュアルFFT測定（Ref=元信号、Meas=変化後の信号）で伝達関数を求める
//   3. 補正EQ案を作らせる
//   4. 提案を適用して測り直し、基準（フラット）に戻るか確認する
// マイクやデバイスがなくてもDSP一式の正しさを検証できる。

#include <gtest/gtest.h>

#include <juce_dsp/juce_dsp.h>

#include "DSP/DelayEstimator.h"
#include "DSP/EQProposer.h"
#include "DSP/ImpulseResponseBuilder.h"
#include "DSP/OctaveSmoother.h"
#include "DSP/PEQ.h"
#include "DSP/TransferFunctionAccumulator.h"

#include <cmath>
#include <complex>
#include <random>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

// テスト用のバイクアッド（時間域。PEQの係数で実信号をフィルタする）
class TestBiquad
{
public:
    void setCoeffs (const eeq::dsp::peq::Coeffs& c) { coeffs = c; }

    float processSample (float x)
    {
        const auto y = coeffs.b0 * x + coeffs.b1 * x1 + coeffs.b2 * x2
                     - coeffs.a1 * y1 - coeffs.a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return (float) y;
    }

private:
    eeq::dsp::peq::Coeffs coeffs;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
};

// ピンクノイズ（Paul Kellet の経済版。仕様 3.2 と同じ方式）
std::vector<float> makePinkNoise (int count, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> white (-1.0f, 1.0f);

    std::vector<float> pink ((size_t) count, 0.0f);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    for (int i = 0; i < count; ++i)
    {
        const auto w = (double) white (rng);
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        pink[(size_t) i] = (float) juce::jlimit (-1.0, 1.0, (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11);
        b6 = w * 0.115926;
    }

    return pink;
}

// デュアルFFT測定をまるごと実行して H(f) を得る。
// FFTサイズはアプリの既定（32768）と同じにする。小さいFFTだとピンクノイズの
// 低域エネルギーのリークで H1 に系統的なバイアスが出る（実測環境でも同じ挙動）
eeq::dsp::SpectrumFrame measure (const std::vector<float>& ref, const std::vector<float>& meas,
                                 int fftSize, int averages)
{
    eeq::dsp::TransferFunctionAccumulator accumulator;
    eeq::dsp::AveragingConfig config;
    config.mode = eeq::dsp::AveragingConfig::Mode::cumulative;
    config.maxAverages = averages;
    accumulator.configure (fftSize, sampleRate, config, juce::dsp::WindowingFunction<float>::hann);

    const auto block = 1024;

    for (size_t start = 0; start < ref.size(); start += (size_t) block)
    {
        const auto n = (int) std::min ((size_t) block, ref.size() - start);
        accumulator.feedSamples (ref.data() + start, meas.data() + start, n);
    }

    eeq::dsp::SpectrumFrame frame;
    accumulator.copyResult (frame);
    return frame;
}

// 指定周波数の振幅 [dB]
double magnitudeAt (const eeq::dsp::SpectrumFrame& frame, double frequency)
{
    const auto binWidth = frame.sampleRate / (double) frame.fftSize;
    const auto k = juce::jlimit (1, (int) frame.transfer.size() - 1, (int) std::round (frequency / binWidth));
    return 20.0 * std::log10 (std::max (std::abs (frame.transfer[(size_t) k]), 1e-9f));
}
} // namespace

TEST (EndToEnd, RestoresKnownPeqWithinSpecTolerance)
{
    // ---- 1. 既知の変化: 1 kHz +6 dB (Q=4) のブーストと 250 Hz -5 dB (Q=2) のカット ----
    eeq::dsp::peq::Band bump;
    bump.type = eeq::dsp::peq::Band::Type::bell;
    bump.frequency = 1000.0;
    bump.gainDb = 6.0;
    bump.q = 4.0;

    eeq::dsp::peq::Band dip;
    dip.type = eeq::dsp::peq::Band::Type::bell;
    dip.frequency = 250.0;
    dip.gainDb = -5.0;
    dip.q = 2.0;

    const auto roomCoeffs = { eeq::dsp::peq::coeffsFor (bump, sampleRate),
                              eeq::dsp::peq::coeffsFor (dip, sampleRate) };

    // ---- 励起信号（ピンクノイズ 30秒）と、Ref=元信号 / Meas=変化後+遅延+ノイズ ----
    const auto count = (int) (30.0 * sampleRate);
    const auto source = makePinkNoise (count, 42);

    std::mt19937 noiseRng (7);
    std::uniform_real_distribution<float> noise (-0.002f, 0.002f); // 軽い暗騒音

    std::vector<float> ref ((size_t) count), meas ((size_t) count);
    const int delaySamples = 500;

    std::vector<TestBiquad> filters;

    for (const auto& coeffs : roomCoeffs)
    {
        TestBiquad filter;
        filter.setCoeffs (coeffs);
        filters.push_back (filter);
    }

    // 部屋の応答（PEQカスケード）を通した信号を作る
    std::vector<float> filteredStream ((size_t) count);

    for (int i = 0; i < count; ++i)
    {
        auto filtered = source[(size_t) i];

        for (auto& filter : filters)
            filtered = filter.processSample (filtered);

        filteredStream[(size_t) i] = filtered;
    }

    for (int i = 0; i < count; ++i)
    {
        ref[(size_t) i] = source[(size_t) i];

        // Meas は 500 サンプル遅れる（音が空間を伝わる時間）
        meas[(size_t) i] = (i >= delaySamples ? filteredStream[(size_t) (i - delaySamples)] : 0.0f)
                         + noise (noiseRng);
    }

    // ---- 2. デュアルFFT測定 ----
    const auto frame = measure (ref, meas, 32768, 60);

    // 測定できているか: 既知の変化が読み取れている（仕様 7.2）
    EXPECT_NEAR (magnitudeAt (frame, 1000.0), 6.0, 0.5);
    EXPECT_NEAR (magnitudeAt (frame, 250.0), -5.0, 0.5);
    EXPECT_NEAR (magnitudeAt (frame, 100.0), 0.0, 0.5);

    // 遅延検出: 相互相関で 500 サンプル ±0.5 に出る（仕様 3.3）。同じ位置の窓を比べる
    const auto delay = eeq::dsp::estimateDelay (ref.data() + 8192, meas.data() + 8192, 16384, true);
    EXPECT_TRUE (delay.valid);
    EXPECT_NEAR (delay.delaySamples, (double) delaySamples, 0.5);

    // インパルス応答のピークも同じ位置に出る（仕様 3.3）
    const auto ir = eeq::dsp::buildImpulseResponse (frame);
    EXPECT_NEAR (ir.peakTimeSamples, (double) delaySamples, 2.0);

    // ---- 3. 補正EQ案（ターゲットはフラット。仕様 4章）----
    eeq::dsp::EQProposalInput input;
    input.sampleRate = sampleRate;
    input.minFrequency = 40.0;
    input.maxFrequency = 16000.0;
    input.maxBands = 8;
    input.allowValleyBoost = false;

    const auto smoothed = eeq::dsp::smoothToLogGrid (frame, 6, input.minFrequency, input.maxFrequency, 200);

    for (const auto& point : smoothed)
    {
        input.frequency.push_back (point.freq);
        input.measuredDb.push_back (20.0 * std::log10 (std::max (std::abs (point.h), 1e-9)));
        input.targetDb.push_back (0.0);
        input.weight.push_back (point.coherence >= 0.8f ? 1.0 : 0.0);
    }

    const auto proposal = eeq::dsp::proposeEQ (input);

    ASSERT_FALSE (proposal.bands.empty());
    EXPECT_LE ((int) proposal.bands.size(), 8);
    EXPECT_TRUE (proposal.converged);

    // ---- 4. 提案を適用して測り直すと、補正範囲で ±1.5 dB 以内に戻る（仕様 4.2 (6) / 7.2）----
    std::vector<TestBiquad> proposalFilters;

    for (const auto& band : proposal.bands)
    {
        TestBiquad filter;
        filter.setCoeffs (eeq::dsp::peq::coeffsFor (band, sampleRate));
        proposalFilters.push_back (filter);
    }

    std::vector<float> corrected ((size_t) count);
    std::vector<TestBiquad> roomAgain = filters; // 部屋の応答はそのまま

    for (int i = 0; i < count; ++i)
    {
        auto sample = source[(size_t) i];

        for (auto& filter : roomAgain)
            sample = filter.processSample (sample);

        for (auto& filter : proposalFilters)
            sample = filter.processSample (sample);

        corrected[(size_t) i] = sample;
    }

    // 補正後の測定（遅延はそのまま残る。振幅だけを見る）
    std::vector<float> ref2 ((size_t) count);

    for (int i = 0; i < count; ++i)
        ref2[(size_t) i] = source[(size_t) i];

    const auto frameAfter = measure (ref2, corrected, 32768, 60);

    // 補正対象範囲（40 Hz〜16 kHz）の残差を確認する
    const auto afterSmoothed = eeq::dsp::smoothToLogGrid (frameAfter, 6, input.minFrequency, input.maxFrequency, 200);
    double maxResidual = 0.0;

    for (const auto& point : afterSmoothed)
    {
        if (point.freq < 50.0 || point.freq > 15000.0)
            continue;

        const auto residual = std::abs (20.0 * std::log10 (std::max (std::abs (point.h), 1e-9)));
        maxResidual = std::max (maxResidual, (double) residual);
    }

    EXPECT_LE (maxResidual, 1.5);
}
