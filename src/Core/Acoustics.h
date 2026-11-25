#pragma once

// 音響に関する換算ユーティリティ（仕様 3.3.2 / 3.8 / 5.6）。
// JUCE に依存しない純粋な計算なので、ユニットテストから直接使える。

namespace eeq::acoustics
{

/** 音速 [m/s]。c ≈ 331.3 + 0.606 × T（T は気温 ℃） */
[[nodiscard]] inline constexpr double speedOfSoundMetresPerSecond (double airTemperatureCelsius) noexcept
{
    return 331.3 + 0.606 * airTemperatureCelsius;
}

/** 距離 [m] → 遅延 [ms]。気温で音速を補正する */
[[nodiscard]] inline constexpr double delayMillisecondsForDistance (double distanceMetres, double airTemperatureCelsius) noexcept
{
    return distanceMetres / speedOfSoundMetresPerSecond (airTemperatureCelsius) * 1000.0;
}

} // namespace eeq::acoustics
