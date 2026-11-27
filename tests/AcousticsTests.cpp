#include <gtest/gtest.h>

#include <limits>

#include "Core/Acoustics.h"

namespace
{
using eeq::acoustics::delayMillisecondsForDistance;
using eeq::acoustics::distanceMetresForDelay;
using eeq::acoustics::speedOfSoundMetresPerSecond;
using eeq::acoustics::airAbsorptionDbPerMetre;
using eeq::acoustics::totalAttenuationDb;
using eeq::acoustics::temperatureFromDelayDistance;
using eeq::acoustics::samplesForDelayMs;
using eeq::acoustics::delayMsForSamples;

TEST (Acoustics, SpeedOfSoundAtReferenceTemperatureIsAbout343)
{
    // 仕様 3.3.2: 既定 20 ℃ で約 343 m/s
    EXPECT_NEAR (speedOfSoundMetresPerSecond (20.0), 343.42, 0.01);
}

TEST (Acoustics, SpeedOfSoundWithHumidity)
{
    // #119: 20 ℃ では 50 %RH で約 +0.19 %、100 %RH で約 +0.37 %
    // （旧実装の √(1 + 1e-5·RH) は 100 %RH でも +0.05 % だった）
    const auto c20dry = speedOfSoundMetresPerSecond (20.0);
    EXPECT_DOUBLE_EQ (speedOfSoundMetresPerSecond (20.0, 0.0), c20dry);
    EXPECT_NEAR (speedOfSoundMetresPerSecond (20.0, 50.0) / c20dry, 1.00187, 0.0002);
    EXPECT_NEAR (speedOfSoundMetresPerSecond (20.0, 100.0) / c20dry, 1.00374, 0.0003);

    // 気温が高いほど飽和水蒸気圧が上がり、補正は大きくなる（30 ℃ 100 %RH で約 +0.67 %）
    EXPECT_NEAR (speedOfSoundMetresPerSecond (30.0, 100.0) / speedOfSoundMetresPerSecond (30.0), 1.0067, 0.0005);
}

TEST (Acoustics, DelayFor30MetresAt20C)
{
    EXPECT_NEAR (delayMillisecondsForDistance (30.0, 20.0), 87.36, 0.05);
}

TEST (Acoustics, WarmingFrom20To30CShortens30mDelayByAboutOneAndHalfMs)
{
    // 仕様 3.8: 距離 30 m の遅延は、気温 20 → 30 ℃ で約 1.5 ms 短くなる
    const auto at20 = delayMillisecondsForDistance (30.0, 20.0);
    const auto at30 = delayMillisecondsForDistance (30.0, 30.0);
    EXPECT_NEAR (at20 - at30, 1.5, 0.05);
}

TEST (Acoustics, DelayDistanceRoundTrip)
{
    const auto dist = 15.5;
    const auto temp = 22.0;
    const auto delay = delayMillisecondsForDistance (dist, temp);
    const auto back = distanceMetresForDelay (delay, temp);
    EXPECT_NEAR (back, dist, 1.0e-9);
}

TEST (Acoustics, SamplesAndMsConversion)
{
    constexpr double sr = 48000.0;
    const auto ms = 10.5;
    const auto samples = samplesForDelayMs (ms, sr);
    const auto back = delayMsForSamples (samples, sr);
    EXPECT_NEAR (back, ms, 1.0e-9);
}

TEST (Acoustics, AirAbsorptionMatchesIso9613ReferenceValues)
{
    // ISO 9613-1:1993, 20 ℃ / 50 % RH, standard pressure.
    struct Reference { double frequency; double absorption; };
    constexpr Reference references[] {
        { 1000.0, 0.0046647 },
        { 4000.0, 0.0296655 },
        { 8000.0, 0.1052909 },
        { 16000.0, 0.3645410 }
    };

    for (const auto& reference : references)
        EXPECT_NEAR (airAbsorptionDbPerMetre (reference.frequency, 50.0, 20.0),
                     reference.absorption, 1.0e-6);
}

TEST (Acoustics, AirAbsorptionIncreasesWithFrequency)
{
    const auto abs1k = airAbsorptionDbPerMetre (1000.0, 50.0, 20.0);
    const auto abs8k = airAbsorptionDbPerMetre (8000.0, 50.0, 20.0);
    EXPECT_GT (abs8k, abs1k); // 高域ほど吸収大
}

TEST (Acoustics, AirAbsorptionHasExpectedTemperatureAndHumidityDirection)
{
    EXPECT_GT (airAbsorptionDbPerMetre (4000.0, 50.0, 10.0),
               airAbsorptionDbPerMetre (4000.0, 50.0, 20.0));
    EXPECT_GT (airAbsorptionDbPerMetre (4000.0, 50.0, 20.0),
               airAbsorptionDbPerMetre (4000.0, 50.0, 30.0));
    EXPECT_GT (airAbsorptionDbPerMetre (4000.0, 20.0, 20.0),
               airAbsorptionDbPerMetre (4000.0, 90.0, 20.0));
}

TEST (Acoustics, AirAbsorptionRejectsInvalidInputs)
{
    EXPECT_EQ (airAbsorptionDbPerMetre (0.0, 50.0, 20.0), 0.0);
    EXPECT_EQ (airAbsorptionDbPerMetre (-1.0, 50.0, 20.0), 0.0);
    EXPECT_EQ (airAbsorptionDbPerMetre (1000.0, -1.0, 20.0), 0.0);
    EXPECT_EQ (airAbsorptionDbPerMetre (1000.0, 101.0, 20.0), 0.0);
    EXPECT_EQ (airAbsorptionDbPerMetre (1000.0, 50.0, -273.15), 0.0);
    EXPECT_EQ (airAbsorptionDbPerMetre (std::numeric_limits<double>::infinity(), 50.0, 20.0), 0.0);
}

TEST (Acoustics, TotalAttenuationIncludesGeometricAndAbsorption)
{
    // 10m, 1kHz: 幾何減衰 -20 dB + 吸収
    const auto att = totalAttenuationDb (10.0, 1000.0, 50.0, 20.0);
    const auto geo = -20.0 * std::log10 (10.0); // -20 dB
    EXPECT_NEAR (att, geo, 0.5); // 吸収は 1kHz では小さいので幾何減衰に近い
}

TEST (Acoustics, TemperatureFromDelayDistance)
{
    const auto dist = 30.0;
    const auto temp = 25.0;
    const auto delay = delayMillisecondsForDistance (dist, temp);
    const auto calcTemp = temperatureFromDelayDistance (delay, dist);
    EXPECT_NEAR (calcTemp, temp, 0.1);
}
} // namespace
