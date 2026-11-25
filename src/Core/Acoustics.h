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

/** 遅延 [ms] → 距離 [m]（気温補正つき。提案値の妥当性チェックに使う: 仕様 3.3.2） */
[[nodiscard]] inline constexpr double distanceMetresForDelay (double delayMilliseconds, double airTemperatureCelsius) noexcept
{
    return delayMilliseconds * speedOfSoundMetresPerSecond (airTemperatureCelsius) / 1000.0;
}

/** 遅延 [ms] → サンプル数 */
[[nodiscard]] inline constexpr double samplesForDelayMs (double delayMilliseconds, double sampleRate) noexcept
{
    return delayMilliseconds * sampleRate / 1000.0;
}

/** サンプル数 → 遅延 [ms] */
[[nodiscard]] inline constexpr double delayMsForSamples (double samples, double sampleRate) noexcept
{
    return samples * 1000.0 / sampleRate;
}

} // namespace eeq::acoustics
