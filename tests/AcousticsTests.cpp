#include <gtest/gtest.h>

#include "Core/Acoustics.h"

namespace
{
using eeq::acoustics::delayMillisecondsForDistance;
using eeq::acoustics::speedOfSoundMetresPerSecond;

TEST (Acoustics, SpeedOfSoundAtReferenceTemperatureIsAbout343)
{
    // 仕様 3.3.2: 既定 20 ℃ で約 343 m/s
    EXPECT_NEAR (speedOfSoundMetresPerSecond (20.0), 343.42, 0.01);
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
} // namespace
