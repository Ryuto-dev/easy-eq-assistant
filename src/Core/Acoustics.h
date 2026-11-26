#pragma once

#include <algorithm>
#include <cmath>

// 音響に関する換算ユーティリティ（仕様 3.3.2 / 3.8 / 5.6）。
// JUCE に依存しない純粋な計算なので、ユニットテストから直接使える。

namespace eeq::acoustics
{

/** 音速 [m/s]。c ≈ 331.3 + 0.606 × T（T は気温 ℃） */
[[nodiscard]] inline constexpr double speedOfSoundMetresPerSecond (double airTemperatureCelsius) noexcept
{
    return 331.3 + 0.606 * airTemperatureCelsius;
}

/** 湿度補正付き音速 [m/s]。ISO 9613-1 近似: c = c0 * sqrt(1 + 0.001 * h / 100)
    h は相対湿度 [%]。通常は 0.1% 程度の影響しかないが、長距離では無視できない。 */
[[nodiscard]] inline double speedOfSoundMetresPerSecond (double airTemperatureCelsius, double humidityPercent) noexcept
{
    const auto c0 = 331.3 + 0.606 * airTemperatureCelsius;
    return c0 * std::sqrt (1.0 + 0.001 * humidityPercent / 100.0);
}

/** 距離 [m] → 遅延 [ms]。気温で音速を補正する */
[[nodiscard]] inline constexpr double delayMillisecondsForDistance (double distanceMetres, double airTemperatureCelsius) noexcept
{
    return distanceMetres / speedOfSoundMetresPerSecond (airTemperatureCelsius) * 1000.0;
}

/** 距離 [m] → 遅延 [ms]。気温と湿度で音速を補正する */
[[nodiscard]] inline double delayMillisecondsForDistance (double distanceMetres, double airTemperatureCelsius, double humidityPercent) noexcept
{
    return distanceMetres / speedOfSoundMetresPerSecond (airTemperatureCelsius, humidityPercent) * 1000.0;
}

/** 遅延 [ms] → 距離 [m]（気温補正つき。提案値の妥当性チェックに使う: 仕様 3.3.2） */
[[nodiscard]] inline constexpr double distanceMetresForDelay (double delayMilliseconds, double airTemperatureCelsius) noexcept
{
    return delayMilliseconds * speedOfSoundMetresPerSecond (airTemperatureCelsius) / 1000.0;
}

/** 遅延 [ms] → 距離 [m]（気温と湿度補正つき） */
[[nodiscard]] inline double distanceMetresForDelay (double delayMilliseconds, double airTemperatureCelsius, double humidityPercent) noexcept
{
    return delayMilliseconds * speedOfSoundMetresPerSecond (airTemperatureCelsius, humidityPercent) / 1000.0;
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

/** 空気吸収による減衰 [dB/m]。簡易近似式（室内・短距離向け、ISO 9613-1 風）。
    frequency: 周波数 [Hz], humidity: 相対湿度 [%], temperature: 気温 [℃]
    有効範囲: 50 Hz〜10 kHz, 10〜100% RH, 15〜30 ℃。範囲外では近似値。
    注: 精密測定用ではない。長距離・高精度用途ではフル実装が必要。 */
[[nodiscard]] inline double airAbsorptionDbPerMetre (double frequencyHz, double humidityPercent, double temperatureCelsius) noexcept
{
    if (frequencyHz <= 0.0 || humidityPercent <= 0.0)
        return 0.0;

    const auto f = frequencyHz / 1000.0; // kHz
    const auto h = std::clamp (humidityPercent, 10.0, 100.0);
    const auto t = std::clamp (temperatureCelsius, 15.0, 30.0);

    // 実用的な簡易近似（ISO 9613-1 Annex A の曲線フィット）
    // α [dB/m] ≈ f^2 * (a + b * h) / (f^2 + c) の形で近似
    // 20℃, 50% RH を基準にした係数
    const auto f2 = f * f;

    // 温度補正係数（20℃基準）
    const auto tempFactor = std::pow (t / 20.0, 0.5); // 概ね平方根則

    // 湿度補正（低湿度で吸収大、高湿度で吸収小）
    const auto humFactor = 1.0 + 0.02 * (50.0 - h) / 50.0;

    // 周波数依存性（低域は f^2 比例、高域で飽和）
    // 1kHz で約 0.001 dB/m, 8kHz で約 0.03 dB/m 程度（20℃, 50% RH）
    const auto baseAbsorption = 0.0001 * f2 / (1.0 + 0.001 * f2) * tempFactor * humFactor;

    return std::max (0.0, baseAbsorption);
}

/** 距離と空気吸収を考慮したレベル減衰 [dB]。
    点音源の逆二乗則（-6 dB/distance doubling）+ 空気吸収。 */
[[nodiscard]] inline double totalAttenuationDb (double distanceMetres, double frequencyHz,
                                                double humidityPercent, double temperatureCelsius) noexcept
{
    if (distanceMetres <= 0.0)
        return 0.0;

    const auto geometric = -20.0 * std::log10 (distanceMetres); // 1m 基準
    const auto absorption = airAbsorptionDbPerMetre (frequencyHz, humidityPercent, temperatureCelsius) * distanceMetres;
    return geometric - absorption; // 空気吸収も損失なので減衰（負）側に加わる
}

/** 気温 [℃] を遅延と距離から逆算（仕様 3.3.2 / 3.8 の妥当性チェック用） */
[[nodiscard]] inline double temperatureFromDelayDistance (double delayMs, double distanceMetres) noexcept
{
    if (delayMs <= 0.0 || distanceMetres <= 0.0)
        return 20.0;

    const auto c = distanceMetres / (delayMs / 1000.0);
    return (c - 331.3) / 0.606;
}

} // namespace eeq::acoustics
