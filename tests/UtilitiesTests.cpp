#include <gtest/gtest.h>

#include "Core/Utilities.h"

#include <cmath>
#include <vector>

TEST (Acoustics, SpeedOfSoundAndDelayRoundTrip)
{
    EXPECT_NEAR (eeq::acoustics::speedOfSoundMetresPerSecond (20.0), 343.42, 0.01);

    const auto delay = eeq::acoustics::delayMillisecondsForDistance (30.0, 20.0);
    EXPECT_NEAR (delay, 87.36, 0.05);
    EXPECT_NEAR (eeq::acoustics::distanceMetresForDelay (delay, 20.0), 30.0, 1.0e-9);
}

TEST (Acoustics, WarmingAirShortensDelay)
{
    const auto at20 = eeq::acoustics::delayMillisecondsForDistance (30.0, 20.0);
    const auto at30 = eeq::acoustics::delayMillisecondsForDistance (30.0, 30.0);

    EXPECT_NEAR (at20 - at30, 1.5, 0.1); // 仕様 3.8 の例
}

TEST (Utilities, Wavelength)
{
    // 20℃・1kHz の波長 ≈ 0.343 m
    EXPECT_NEAR (eeq::util::wavelengthMetres (1000.0, 20.0), 0.34342, 0.001);
    EXPECT_NEAR (eeq::util::halfWavelengthMetres (1000.0, 20.0), 0.17171, 0.001);
    EXPECT_NEAR (eeq::util::quarterWavelengthMetres (1000.0, 20.0), 0.085855, 0.001);
}

TEST (Utilities, CombFilterNullsAndPeaks)
{
    // 遅延 5ms → 最初のキャンセルは 100 Hz、次は 300 Hz（奇数倍。仕様 5.6）
    EXPECT_NEAR (eeq::util::combFilterNullFrequency (5.0, 1), 100.0, 0.01);
    EXPECT_NEAR (eeq::util::combFilterNullFrequency (5.0, 2), 300.0, 0.01);
    EXPECT_NEAR (eeq::util::combFilterNullFrequency (5.0, 3), 500.0, 0.01);

    // ピーク周波数: f = k / Δt
    EXPECT_NEAR (eeq::util::combFilterPeakFrequency (5.0, 1), 200.0, 0.01);
    EXPECT_NEAR (eeq::util::combFilterPeakFrequency (5.0, 2), 400.0, 0.01);
}

TEST (Utilities, DistanceAttenuation)
{
    // 距離が2倍で −6 dB
    EXPECT_NEAR (eeq::util::distanceAttenuationDb (10.0, 20.0), -6.02, 0.02);
    EXPECT_NEAR (eeq::util::distanceAttenuationDb (10.0, 5.0), 6.02, 0.02);

    // 線音源: 距離が2倍で −3 dB
    EXPECT_NEAR (eeq::util::lineSourceAttenuationDb (10.0, 20.0), -3.01, 0.02);

    // 面音源: 減衰なし
    EXPECT_NEAR (eeq::util::planeSourceAttenuationDb (10.0, 20.0), 0.0, 1.0e-9);
}

TEST (Utilities, DecibelSum)
{
    const double levels[] = { 90.0, 90.0 };
    EXPECT_NEAR (eeq::util::sumDecibels (levels, 2), 93.01, 0.02);

    const double single[] = { 80.0 };
    EXPECT_NEAR (eeq::util::sumDecibels (single, 1), 80.0, 1.0e-9);

    // ベクター版
    EXPECT_NEAR (eeq::util::sumDecibels (std::vector<double>{90.0, 90.0}), 93.01, 0.02);

    // コヒーレント合成（同位相なら +6 dB）
    EXPECT_NEAR (eeq::util::sumDecibelsCoherent (levels, 2), 96.02, 0.02);
}

TEST (Utilities, LevelConversions)
{
    EXPECT_NEAR (eeq::util::voltsToDbu (0.7746), 0.0, 0.001);
    EXPECT_NEAR (eeq::util::voltsToDbv (1.0), 0.0, 0.001);
    EXPECT_NEAR (eeq::util::dbuToVolts (0.0), 0.7746, 0.001);
    EXPECT_NEAR (eeq::util::dbvToVolts (0.0), 1.0, 0.001);

    // dBu と dBV の差は 2.21 dB
    EXPECT_NEAR (eeq::util::voltsToDbu (1.0) - eeq::util::voltsToDbv (1.0), 2.21, 0.01);

    // 往復変換
    EXPECT_NEAR (eeq::util::dbuToVolts (eeq::util::voltsToDbu (2.5)), 2.5, 1.0e-9);

    // dBFS → dBu/dBV（ヘッドルーム +18 dB）
    EXPECT_NEAR (eeq::util::dbfsToDbu (-18.0), 0.0, 0.001);
    EXPECT_NEAR (eeq::util::dbfsToDbv (-18.0), 0.0, 0.001);
}

TEST (Utilities, MicCalibrationOffset)
{
    // ECM8000 風: 50 mV/Pa → -26 dBV/Pa → オフセット ≈ 120 dB
    // 94 dB SPL = 1 Pa → 出力 -26 dBV → 94 - (-26) = 120 dB オフセット
    const auto offset = eeq::util::calibrationOffsetFromMicSensitivity (50.0); // mV/Pa
    EXPECT_NEAR (offset, 120.0, 1.0);

    // 既に dBV/Pa で与えられている場合
    const auto offset2 = eeq::util::calibrationOffsetFromMicSensitivity (-26.0); // dBV/Pa
    EXPECT_NEAR (offset2, 120.0, 1.0);
}

TEST (Utilities, Tempo)
{
    EXPECT_NEAR (eeq::util::tempoToMilliseconds (120.0), 500.0, 0.001);
    EXPECT_NEAR (eeq::util::tempoToMilliseconds (120.0, 0.5), 250.0, 0.001);

    // 往復
    EXPECT_NEAR (eeq::util::millisecondsToTempo (500.0), 120.0, 0.001);
    EXPECT_NEAR (eeq::util::millisecondsToTempo (250.0, 0.5), 120.0, 0.001);
}

TEST (Utilities, QBandwidthRoundTrip)
{
    // 既知のペア: 1オクターブ ↔ Q=1.4142（sqrt(2)/1）
    EXPECT_NEAR (eeq::util::bandwidthOctavesToQ (1.0), 1.4142, 0.01);

    // 2オクターブ ↔ Q=0.667
    EXPECT_NEAR (eeq::util::bandwidthOctavesToQ (2.0), 0.6667, 0.01);

    // 往復で元に戻る
    for (double q = 0.5; q <= 10.0; q *= 1.6)
        EXPECT_NEAR (eeq::util::bandwidthOctavesToQ (eeq::util::qToBandwidthOctaves (q)), q, 1.0e-6);
}

TEST (Utilities, DbGainPowerConversions)
{
    // dB ↔ 線形振幅
    EXPECT_NEAR (eeq::util::dbToGain (6.02), 2.0, 0.01);
    EXPECT_NEAR (eeq::util::gainToDb (2.0), 6.02, 0.01);
    EXPECT_NEAR (eeq::util::gainToDb (eeq::util::dbToGain (3.5)), 3.5, 1.0e-9);

    // dB ↔ 線形パワー
    EXPECT_NEAR (eeq::util::dbToPower (10.0), 10.0, 0.01);
    EXPECT_NEAR (eeq::util::powerToDb (10.0), 10.0, 0.01);
    EXPECT_NEAR (eeq::util::powerToDb (eeq::util::dbToPower (5.0)), 5.0, 1.0e-9);
}

TEST (Utilities, HeadroomAndCrestFactor)
{
    // ヘッドルーム: 目標 -6 dBFS, 現在 -10 dBFS → 4 dB 余裕
    EXPECT_NEAR (eeq::util::headroomDb (-10.0), 4.0, 1.0e-9);

    // クレストファクター: ピーク -3 dBFS, RMS -12 dBFS → 9 dB
    EXPECT_NEAR (eeq::util::crestFactorDb (-3.0, -12.0), 9.0, 1.0e-9);

    // 圧縮量: 入力 +6 dB, SPL上昇 +4 dB → 圧縮 2 dB
    EXPECT_NEAR (eeq::util::compressionAmountDb (6.0, 4.0), 2.0, 1.0e-9);
    EXPECT_NEAR (eeq::util::compressionAmountDb (6.0, 6.0), 0.0, 1.0e-9);
    EXPECT_NEAR (eeq::util::compressionAmountDb (6.0, 7.0), 0.0, 1.0e-9); // 負にならない
}

TEST (Utilities, ClipDb)
{
    EXPECT_NEAR (eeq::util::clipDb (5.0, 0.0, 10.0), 5.0, 1.0e-9);
    EXPECT_NEAR (eeq::util::clipDb (-5.0, 0.0, 10.0), 0.0, 1.0e-9);
    EXPECT_NEAR (eeq::util::clipDb (15.0, 0.0, 10.0), 10.0, 1.0e-9);
}
