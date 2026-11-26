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

/** 湿度補正付き音速 [m/s]。経験的な近似で、ISO 9613-1 の吸収式とは別の計算。
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

/** 空気吸収による減衰 [dB/m]。ISO 9613-1:1993 Annex A（標準気圧）による。
    frequency: 周波数 [Hz], humidity: 相対湿度 [%], temperature: 気温 [℃]
    相対湿度は 0〜100% とし、周波数は正の有限値、絶対温度は 0 K より大きい値を受け付ける。
    入力が物理的に無効または計算結果が有限でない場合は 0 を返す。 */
[[nodiscard]] inline double airAbsorptionDbPerMetre (double frequencyHz, double humidityPercent, double temperatureCelsius) noexcept
{
    if (! std::isfinite (frequencyHz) || ! std::isfinite (humidityPercent)
        || ! std::isfinite (temperatureCelsius) || frequencyHz <= 0.0
        || humidityPercent < 0.0 || humidityPercent > 100.0
        || temperatureCelsius <= -273.15)
        return 0.0;

    // ISO 9613-1: pressure is fixed at the reference pressure (p_a / p_r = 1).
    // The saturation-pressure equation uses the triple point; the relaxation
    // and attenuation equations use the separate 20 °C reference temperature.
    constexpr auto referenceTemperature = 293.15;
    constexpr auto triplePointTemperature = 273.16;
    constexpr auto pressureRatio = 1.0;
    const auto T = temperatureCelsius + 273.15;
    const auto h = humidityPercent * std::pow (10.0, -6.8346 * std::pow (triplePointTemperature / T, 1.261) + 4.6151)
                 / pressureRatio;
    const auto f2 = frequencyHz * frequencyHz;

    if (! std::isfinite (T) || ! std::isfinite (h) || ! std::isfinite (f2))
        return 0.0;

    const auto frO = pressureRatio * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const auto temperatureRatio = T / referenceTemperature;
    const auto frN = pressureRatio * std::pow (temperatureRatio, -0.5)
                   * (9.0 + 280.0 * h * std::exp (-4.17 * (std::pow (temperatureRatio, -1.0 / 3.0) - 1.0)));
    const auto t1 = 1.84e-11 * pressureRatio * std::pow (temperatureRatio, 0.5);
    const auto t2 = std::pow (temperatureRatio, -2.5)
                  * (0.01275 * std::exp (-2239.1 / T) / (frO + f2 / frO)
                   + 0.1068 * std::exp (-3352.0 / T) / (frN + f2 / frN));
    const auto absorption = 8.686 * f2 * (t1 + t2);

    return std::isfinite (absorption) && absorption >= 0.0 ? absorption : 0.0;
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
