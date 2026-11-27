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

TEST (RtaSpl, PeakIsSineRmsReferencedLpeak)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.5);
    analyzer.setCalibrationOffsetDb (94.0);

    feed (analyzer, makeSine (1000.0, 0.5, 1.5));

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copySpl (spl);

    ASSERT_TRUE (spl.hasSignal);
    // #112: 0 dB は正弦波の RMS 基準。振幅 0.5 の正弦波は Leq = 20log(0.5) + 94 = 87.98 dB、
    // Lpeak はその +3.01 dB = 90.99 dB（旧実装は 87.98 dB を返していた）
    EXPECT_NEAR (spl.peakDb, 20.0 * std::log10 (0.5) + 94.0 + 3.0103, 0.01);
    EXPECT_NEAR (spl.peakDb - spl.leqSessionDb, 3.0103, 0.05);
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

    // 時間領域の SPL では Fast の時定数だと 47 Hz のリップル（±0.06 dB）が残るので Slow で読む
    analyzer.setTimeConstant (eeq::dsp::RtaSplAnalyzer::TimeConstant::slow);
    feed (analyzer, makeSine (4.0 * sampleRate / (double) (1 << eeq::dsp::RtaSplAnalyzer::fftOrder), 1.0, 8.0));

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copySpl (spl);

    ASSERT_TRUE (spl.hasSignal);
    EXPECT_NEAR (spl.currentDb, 0.0, 0.05);
    EXPECT_NEAR (spl.leqSessionDb, 0.0, 0.05);
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

    // 帯域中心そのものの周波数を使う（IEC 61260: 1000·10^(0.3n/3)。n=6 → 3981 Hz）
    const auto toneFreq = eeq::dsp::RtaSplAnalyzer::exactBandCenter (6, 3);
    feed (analyzer, makeSine (toneFreq, 0.5, 1.5));

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

    EXPECT_NEAR (frame.centerFreq[(size_t) best], toneFreq, 1.0e-6);
    EXPECT_DOUBLE_EQ (frame.nominalFreq[(size_t) best], 4000.0);
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

namespace
{
// IEC 61672-1 のトーンバースト試験: 定常の 4 kHz 正弦波の表示値に対する、
// 長さ T のバーストの最大表示値の差 [dB]
double toneburstResponseDb (eeq::dsp::RtaSplAnalyzer::TimeConstant timeConstant, double burstSeconds)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.5);
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted);
    analyzer.setTimeConstant (timeConstant);

    // 定常値（Slow でも十分収束する 8 秒）
    feed (analyzer, makeSine (4000.0, 0.5, 8.0));
    eeq::dsp::RtaSplAnalyzer::SplSnapshot steady;
    analyzer.copySpl (steady);

    analyzer.resetSpl();
    feed (analyzer, std::vector<float> ((size_t) sampleRate, 0.0f)); // 1 秒の無音

    // 4 kHz は 12 サンプル周期なので、バーストは整数周期で終わる
    const auto burst = makeSine (4000.0, 0.5, burstSeconds);
    double maxDb = -300.0;

    for (size_t start = 0; start < burst.size(); start += 16)
    {
        analyzer.process (burst.data() + start, (int) std::min ((size_t) 16, burst.size() - start));
        eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
        analyzer.copySpl (spl);
        maxDb = std::max (maxDb, spl.currentDb);
    }

    return maxDb - steady.currentDb;
}
} // namespace

// #111: Fast / Slow は時間領域で 2 乗 → 指数平滑する。IEC 61672-1 表 4 の基準値
// （10·log10(1 − e^(−T/τ))）に、Class 1 の許容差より十分小さい ±0.3 dB で一致する。
// 旧実装（8192 点 Hann 窓のフレームエネルギーを平滑）は 200 ms −1.35 / 10 ms −12.6 / 1 ms −22.6 だった
TEST (RtaSpl, FastToneburstResponseMatchesIec61672)
{
    using TC = eeq::dsp::RtaSplAnalyzer::TimeConstant;
    EXPECT_NEAR (toneburstResponseDb (TC::fast, 0.200), -1.0, 0.3);
    EXPECT_NEAR (toneburstResponseDb (TC::fast, 0.010), -11.0, 0.3);
    EXPECT_NEAR (toneburstResponseDb (TC::fast, 0.001), -21.0, 0.3);
}

TEST (RtaSpl, SlowToneburstResponseMatchesIec61672)
{
    using TC = eeq::dsp::RtaSplAnalyzer::TimeConstant;
    EXPECT_NEAR (toneburstResponseDb (TC::slow, 0.500), -4.1, 0.3);
    EXPECT_NEAR (toneburstResponseDb (TC::slow, 0.200), -7.4, 0.3);
}

// #111: SPL 経路の重み付け IIR は、20 Hz〜16 kHz で IEC 61672 の定義式に ±0.5 dB で一致する
TEST (RtaSpl, WeightingIirMatchesDefinition)
{
    using W = eeq::dsp::RtaSplAnalyzer::Weighting;

    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
    {
        for (int n = -17; n <= 12; ++n)
        {
            const auto f = eeq::dsp::RtaSplAnalyzer::exactBandCenter (n, 10);

            EXPECT_NEAR (eeq::dsp::RtaSplAnalyzer::weightingFilterResponseDb (W::aWeighted, f, fs),
                         eeq::dsp::RtaSplAnalyzer::aWeightingDb (f), 0.5) << "A fs=" << fs << " f=" << f;
            EXPECT_NEAR (eeq::dsp::RtaSplAnalyzer::weightingFilterResponseDb (W::cWeighted, f, fs),
                         eeq::dsp::RtaSplAnalyzer::cWeightingDb (f), 0.5) << "C fs=" << fs << " f=" << f;
        }

        EXPECT_NEAR (eeq::dsp::RtaSplAnalyzer::weightingFilterResponseDb (W::zWeighted, 50.0, fs), 0.0, 1.0e-12);
    }
}

// #116: 帯域中心は IEC 61260 の基数10（1000·10^(0.3n/b)）。1 kHz バンドの中心が 1000 Hz ちょうどで、
// 48 kHz では上端が fs/2 未満なので 20 kHz バンドを含む（旧実装は 1015.9 Hz、最上位 16.26 kHz）
TEST (RtaSpl, BandCentresFollowIec61260Base10)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (48000.0, 3, 0.5);

    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    analyzer.copyRta (frame);

    ASSERT_EQ (frame.centerFreq.size(), (size_t) 31); // 20 Hz〜20 kHz の 1/3 oct は 31 バンド
    ASSERT_EQ (frame.nominalFreq.size(), frame.centerFreq.size());

    EXPECT_NEAR (frame.centerFreq.front(), 19.953, 0.001);
    EXPECT_DOUBLE_EQ (frame.nominalFreq.front(), 20.0);
    EXPECT_NEAR (frame.centerFreq[17], 1000.0, 1.0e-9);
    EXPECT_DOUBLE_EQ (frame.nominalFreq[17], 1000.0);
    EXPECT_NEAR (frame.centerFreq.back(), 19952.6, 0.1);
    EXPECT_DOUBLE_EQ (frame.nominalFreq.back(), 20000.0);

    const double expectedNominal[] = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
                                       630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                                       10000, 12500, 16000, 20000 };

    for (size_t i = 0; i < frame.nominalFreq.size(); ++i)
        EXPECT_NEAR (frame.nominalFreq[i], expectedNominal[i], 1.0e-9) << i;

    // 44.1 kHz では 20 kHz バンドの上端（22.39 kHz）が fs/2 を超えるので含めない
    eeq::dsp::RtaSplAnalyzer analyzer441;
    analyzer441.configure (44100.0, 3, 0.5);
    analyzer441.copyRta (frame);
    ASSERT_EQ (frame.centerFreq.size(), (size_t) 30);
    EXPECT_DOUBLE_EQ (frame.nominalFreq.back(), 16000.0);

    // 1/6 oct: 1/3 oct と共通の帯域は R10 の公称値、それ以外は有効数字3桁
    EXPECT_DOUBLE_EQ (eeq::dsp::RtaSplAnalyzer::nominalBandFrequency (eeq::dsp::RtaSplAnalyzer::exactBandCenter (2, 6), 6),
                      1250.0);
    EXPECT_DOUBLE_EQ (eeq::dsp::RtaSplAnalyzer::nominalBandFrequency (eeq::dsp::RtaSplAnalyzer::exactBandCenter (1, 6), 6),
                      1120.0);
}

// #119 (3): 重み付けを切り替えたら Leq をリセットする（A と C を混ぜない）
TEST (RtaSpl, ChangingWeightingRestartsLeq)
{
    eeq::dsp::RtaSplAnalyzer analyzer;
    analyzer.configure (sampleRate, 3, 0.5);
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted);
    analyzer.setTimeConstant (eeq::dsp::RtaSplAnalyzer::TimeConstant::fast);

    // 100 Hz: A = −19.1 dB、C = −0.3 dB
    const auto tone = makeSine (100.0, 0.5, 3.0);
    feed (analyzer, tone);

    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::cWeighted);
    feed (analyzer, tone);

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    analyzer.copySpl (spl);

    const auto expectedC = 20.0 * std::log10 (0.5) + eeq::dsp::RtaSplAnalyzer::cWeightingDb (100.0);

    // 混ざっていると (A と C のエネルギー平均で) 約 −3 dB 低く出る
    EXPECT_NEAR (spl.leqSessionDb, expectedC, 0.1);
    EXPECT_NEAR (spl.leq1minDb, expectedC, 0.1);
    EXPECT_NEAR (spl.sessionSeconds, 3.0, 0.01);
}
