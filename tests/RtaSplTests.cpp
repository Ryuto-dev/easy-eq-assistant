#include <gtest/gtest.h>

#include "DSP/RtaSplAnalyzer.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

std::vector<float> makeSine (double frequencyHz, double amplitude, double seconds)
{
    const auto count = (int) (seconds * sampleRate);
    std::vector<float> samples ((size_t) count);

    for (int i = 0; i < count; ++i)
        samples[(size_t) i] = (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequencyHz * (double) i / sampleRate));

    return samples;
}

std::vector<float> makeWhiteNoise (double seconds, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);

    std::vector<float> samples ((size_t) (seconds * sampleRate));

    for (auto& sample : samples)
        sample = dist (rng) * 0.3f;

    return samples;
}

void feed (eeq::dsp::RtaSplAnalyzer& analyzer, const std::vector<float>& samples)
{
    const int chunk = 512;

    for (size_t start = 0; start < samples.size(); start += (size_t) chunk)
        analyzer.process (samples.data() + start,
                          (int) std::min ((size_t) chunk, samples.size() - start));
}
} // namespace

TEST (RtaSpl, AWeightingMatchesIecTable)
{
    using namespace eeq::dsp;
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (1000.0), 0.0, 0.01);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (31.5), -39.4, 0.3);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (63.0), -26.2, 0.3);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (125.0), -16.1, 0.2);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (1000.0), 0.0, 0.1);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (2000.0), 1.2, 0.2);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (4000.0), 1.0, 0.2);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (8000.0), -1.1, 0.3);
    EXPECT_NEAR (RtaSplAnalyzer::aWeightingDb (16000.0), -6.6, 0.4);
}

TEST (RtaSpl, CWeightingMatchesIecTable)
{
    using namespace eeq::dsp;
    EXPECT_NEAR (RtaSplAnalyzer::cWeightingDb (1000.0), 0.0, 0.01);
    EXPECT_NEAR (RtaSplAnalyzer::cWeightingDb (63.0), -0.8, 0.2);
    EXPECT_NEAR (RtaSplAnalyzer::cWeightingDb (125.0), -0.2, 0.15);
    EXPECT_NEAR (RtaSplAnalyzer::cWeightingDb (4000.0), -0.75, 0.25);
    EXPECT_NEAR (RtaSplAnalyzer::cWeightingDb (8000.0), -3.0, 0.3);
}

TEST (RtaSpl, PeakReadsSampleAmplitudePlusOffset)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.5);
    analyzer.setCalibrationOffsetDb (94.0);

    feed (analyzer, makeSine (1000.0, 0.5, 1.5));

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copySpl (spl);

    ASSERT_TRUE (spl.hasSignal);
    EXPECT_NEAR (spl.peakDb, 20.0 * std::log10 (0.5) + 94.0, 0.01);
}

TEST (RtaSpl, WeightingDifferenceMatchesDefinition)
{
    // 同じ振幅の 1 kHz と 4 kHz の正弦波の A重み表示差は、定義表の差 (+1.0 dB) に一致する
    eeq::dsp::RtaSplAnalyzer analyzerA;
    analyzerA.configure (sampleRate, 3, 0.3);
    analyzerA.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted);
    feed (analyzerA, makeSine (1000.0, 0.5, 1.5));
    eeq::dsp::RtaSplAnalyzer::SplSnapshot at1k;
    analyzerA.copySpl (at1k);

    eeq::dsp::RtaSplAnalyzer analyzerB;
    analyzerB.configure (sampleRate, 3, 0.3);
    analyzerB.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted);
    feed (analyzerB, makeSine (4000.0, 0.5, 1.5));
    eeq::dsp::RtaSplAnalyzer::SplSnapshot at4k;
    analyzerB.copySpl (at4k);

    const auto expected = eeq::dsp::RtaSplAnalyzer::aWeightingDb (4000.0)
                        - eeq::dsp::RtaSplAnalyzer::aWeightingDb (1000.0);

    EXPECT_NEAR (at4k.currentDb - at1k.currentDb, expected, 0.3);
}

TEST (RtaSpl, LeqMatchesSteadyLevel)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.5);
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::zWeighted);

    feed (analyzer, makeSine (1000.0, 0.4, 2.0));

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copySpl (spl);

    ASSERT_TRUE (spl.hasSignal);
    EXPECT_NEAR (spl.leq1minDb, spl.currentDb, 0.2);
    EXPECT_NEAR (spl.leqSessionDb, spl.currentDb, 0.2);
}

TEST (RtaSpl, FullBandReferenceMakesBinCenteredFullScaleSineZeroDbfs)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.1);
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::zWeighted);

    feed (analyzer, makeSine (4.0 * sampleRate / (double) (1 << eeq::dsp::RtaSplAnalyzer::fftOrder), 1.0, 3.0));

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copySpl (spl);

    ASSERT_TRUE (spl.hasSignal);
    EXPECT_NEAR (spl.currentDb, 0.0, 0.05);
}

TEST (RtaSpl, AdjacentLowBandsPartitionBoundaryBinPower)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.1);
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::zWeighted);

    // Bin 4 (23.4375 Hz) is in the low-frequency range where rounded inclusive
    // ranges assigned the same bin to both adjacent 1/3-octave bands. Some of
    // the window's leakage falls below the first 20 Hz band edge, so compare
    // against full-band SPL with a margin for that expected out-of-band energy.
    feed (analyzer, makeSine (4.0 * sampleRate / (double) (1 << eeq::dsp::RtaSplAnalyzer::fftOrder), 1.0, 3.0));

    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copyRta (frame);
    analyzer.copySpl (spl);

    double rtaPower = 0.0;
    for (const auto level : frame.levelDb)
        rtaPower += std::pow (10.0, (double) level / 10.0);

    const auto splPower = std::pow (10.0, spl.currentDb / 10.0);
    const auto rtaToSplPowerRatio = rtaPower / splPower;
    EXPECT_GT (rtaToSplPowerRatio, 0.85); // retain most in-band power
    EXPECT_LT (rtaToSplPowerRatio, 1.01); // do not count any bin twice
}

TEST (RtaSpl, ToneLandsInCorrectBand)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.5);

    // 帯域中心そのものの周波数を使う
    // （中心は 20·2^(k/3) の幾何系列）
    feed (analyzer, makeSine (20.0 * std::pow (2.0, 23.0 / 3.0), 0.5, 1.5));

    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    analyzer.copyRta (frame);

    ASSERT_FALSE (frame.levelDb.empty());

    // レベルが最も高いバンドが、正弦波の周波数を含むバンドと一致するはず
    int best = 0;
    float bestLevel = -1.0e9f;

    for (size_t i = 0; i < frame.levelDb.size(); ++i)
    {
        if (frame.levelDb[i] > bestLevel)
        {
            bestLevel = frame.levelDb[i];
            best = (int) i;
        }
    }

    const auto toneFreq = 20.0 * std::pow (2.0, 23.0 / 3.0);
    EXPECT_NEAR (frame.centerFreq[(size_t) best], toneFreq, toneFreq * 0.05);
}

TEST (RtaSpl, WhiteNoiseBandsRiseThreeDbPerOctave)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 1.0);
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::zWeighted);

    feed (analyzer, makeWhiteNoise (2.5, 999));

    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    analyzer.copyRta (frame);

    // 白色ノイズは bin あたり一定パワーなので、1/3 oct バンド幅は1オクターブで2倍:
    // バンドレベルは +3.01 dB/oct で上がる。200Hz〜5kHzで最小二乗近似して確認する
    std::vector<double> logF, level;

    for (size_t i = 0; i < frame.centerFreq.size(); ++i)
    {
        if (frame.centerFreq[i] < 200.0 || frame.centerFreq[i] > 5000.0)
            continue;

        if (frame.levelDb[i] < -200.0f)
            continue;

        logF.push_back (std::log2 (frame.centerFreq[i]));
        level.push_back (frame.levelDb[i]);
    }

    ASSERT_GE (logF.size(), (size_t) 10);

    double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    const auto n = (double) logF.size();

    for (size_t i = 0; i < logF.size(); ++i)
    {
        sumX += logF[i];
        sumY += level[i];
        sumXY += logF[i] * level[i];
        sumXX += logF[i] * logF[i];
    }

    const auto slope = (n * sumXY - sumX * sumY) / (n * sumXX - sumX * sumX);
    EXPECT_NEAR (slope, 3.01, 0.8);
}

// issue #48: 比較校正は「既存オフセットを外した生の値」を基準に差を取る。
// 2 回続けて校正しても同じオフセットになる（旧実装は 2 回目で 0 に戻った）
TEST (RtaSpl, CalibrationOffsetCanBeAppliedTwice)
{
    // 1 回目: オフセット 0 のとき表示 = 生値
    EXPECT_NEAR (eeq::dsp::RtaSplAnalyzer::calibrationOffsetFor (94.0, -40.0, 0.0), 134.0, 1e-9);

    // 2 回目: 表示はオフセット込み（-40 + 134 = 94）。同じ K が得られる
    const auto displayed = -40.0 + 134.0;
    EXPECT_NEAR (eeq::dsp::RtaSplAnalyzer::calibrationOffsetFor (94.0, displayed, 134.0), 134.0, 1e-9);

    // 値が変わった場合も差分が正しく取れる
    EXPECT_NEAR (eeq::dsp::RtaSplAnalyzer::calibrationOffsetFor (90.0, 96.0, 134.0), 128.0, 1e-9);
}

// issue #49: RTA スケールの上端は校正オフセットそのもの（94 を足さない）
TEST (RtaSpl, RtaScaleTopUsesOffsetDirectly)
{
    EXPECT_EQ (eeq::dsp::RtaSplAnalyzer::rtaScaleTopDb (0.0), 0.0f);
    EXPECT_EQ (eeq::dsp::RtaSplAnalyzer::rtaScaleTopDb (134.0), 134.0f);
    EXPECT_EQ (eeq::dsp::RtaSplAnalyzer::rtaScaleTopDb (-6.0), -6.0f);
}
