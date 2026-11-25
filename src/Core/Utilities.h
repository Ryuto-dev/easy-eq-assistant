#pragma once

#include "Acoustics.h"

#include <cmath>

// 計算ユーティリティ（仕様 5.6 の8ツール + 遅延換算）。
// JUCE に依存しない純粋な計算なので、ユニットテストから直接使える。
// 遅延・音速の換算は Acoustics.h（eeq::acoustics）にまとめてある。

namespace eeq::util
{

/** 波長 [m]（仕様 5.6: 周波数⇄波長） */
[[nodiscard]] inline constexpr double wavelengthMetres (double frequencyHz, double airTemperatureCelsius = 20.0) noexcept
{
    return acoustics::speedOfSoundMetresPerSecond (airTemperatureCelsius) / frequencyHz;
}

/** コムフィルタのキャンセル周波数 [Hz]（仕様 5.6: f = 1/(2Δt) とその奇数倍）。
    k = 1, 2, 3... に対して f_k = (2k−1) / (2·Δt) */
[[nodiscard]] inline constexpr double combFilterNullFrequency (double delayMs, int multiple) noexcept
{
    if (delayMs <= 0.0)
        return 0.0;

    return (2.0 * (double) multiple - 1.0) / (2.0 * delayMs / 1000.0);
}

/** 距離減衰 [dB]（点音源の逆二乗則。距離が2倍で −6 dB。仕様 5.6） */
[[nodiscard]] inline double distanceAttenuationDb (double fromMetres, double toMetres) noexcept
{
    if (fromMetres <= 0.0 || toMetres <= 0.0)
        return 0.0;

    return -20.0 * std::log10 (toMetres / fromMetres);
}

/** dB の非コヒーレント合成（複数音源の dB 合成。仕様 5.6） */
[[nodiscard]] inline double sumDecibels (const double* levelsDb, int count) noexcept
{
    if (levelsDb == nullptr || count <= 0)
        return -300.0;

    double sum = 0.0;

    for (int i = 0; i < count; ++i)
        sum += std::pow (10.0, levelsDb[i] / 10.0);

    return 10.0 * std::log10 (sum);
}

/** 電圧 [V] → dBu（0 dBu = 0.7746 V） */
[[nodiscard]] inline double voltsToDbu (double volts) noexcept
{
    return volts > 0.0 ? 20.0 * std::log10 (volts / 0.7746) : -300.0;
}

/** 電圧 [V] → dBV（0 dBV = 1 V） */
[[nodiscard]] inline double voltsToDbv (double volts) noexcept
{
    return volts > 0.0 ? 20.0 * std::log10 (volts) : -300.0;
}

/** dBu → 電圧 [V] */
[[nodiscard]] inline double dbuToVolts (double dbu) noexcept
{
    return 0.7746 * std::pow (10.0, dbu / 20.0);
}

/** dBV → 電圧 [V] */
[[nodiscard]] inline double dbvToVolts (double dbv) noexcept
{
    return std::pow (10.0, dbv / 20.0);
}

/** BPM → 1拍の時間 [ms]（仕様 5.6: テンポ⇄時間）。beats で分音符を表す（1=4分, 2=8分...） */
[[nodiscard]] inline constexpr double tempoToMilliseconds (double bpm, double beats = 1.0) noexcept
{
    return bpm > 0.0 ? beats * 60000.0 / bpm : 0.0;
}

/** Q → バンド幅 [oct]（−3 dB幅。ベルフィルタの定義から） */
[[nodiscard]] inline double qToBandwidthOctaves (double q) noexcept
{
    if (q <= 0.0)
        return 0.0;

    // Q = sqrt(2^N)/(2^N − 1) を解く: 2^N = (2Q²+1 + sqrt(4Q²+1)) / (2Q²)
    const auto q2 = q * q;
    const auto x = ((2.0 * q2 + 1.0) + std::sqrt (4.0 * q2 + 1.0)) / (2.0 * q2);

    return std::log2 (x);
}

/** バンド幅 [oct] → Q */
[[nodiscard]] inline double bandwidthOctavesToQ (double bandwidthOctaves) noexcept
{
    if (bandwidthOctaves <= 0.0)
        return 10.0;

    const auto x = std::pow (2.0, bandwidthOctaves);
    return std::sqrt (x) / (x - 1.0);
}

} // namespace eeq::util
