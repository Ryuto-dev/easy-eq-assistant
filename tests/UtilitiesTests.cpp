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
}

TEST (Utilities, CombFilterNulls)
{
    // 遅延 5ms → 最初のキャンセルは 100 Hz、次は 300 Hz（奇数倍。仕様 5.6）
    EXPECT_NEAR (eeq::util::combFilterNullFrequency (5.0, 1), 100.0, 0.01);
    EXPECT_NEAR (eeq::util::combFilterNullFrequency (5.0, 2), 300.0, 0.01);
    EXPECT_NEAR (eeq::util::combFilterNullFrequency (5.0, 3), 500.0, 0.01);
}

TEST (Utilities, DistanceAttenuation)
{
    // 距離が2倍で −6 dB
    EXPECT_NEAR (eeq::util::distanceAttenuationDb (10.0, 20.0), -6.02, 0.02);
    EXPECT_NEAR (eeq::util::distanceAttenuationDb (10.0, 5.0), 6.02, 0.02);
}

TEST (Utilities, DecibelSum)
{
    const double levels[] = { 90.0, 90.0 };
    EXPECT_NEAR (eeq::util::sumDecibels (levels, 2), 93.01, 0.02);

    const double single[] = { 80.0 };
    EXPECT_NEAR (eeq::util::sumDecibels (single, 1), 80.0, 1.0e-9);
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
}

TEST (Utilities, Tempo)
{
    EXPECT_NEAR (eeq::util::tempoToMilliseconds (120.0), 500.0, 0.001);
    EXPECT_NEAR (eeq::util::tempoToMilliseconds (120.0, 0.5), 250.0, 0.001);
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
