#pragma once

#include "Acoustics.h"

#include <cmath>
#include <string>
#include <vector>

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

/** 半波長・1/4波長 [m]（ルームモードやキャンセルの目安） */
[[nodiscard]] inline constexpr double halfWavelengthMetres (double frequencyHz, double airTemperatureCelsius = 20.0) noexcept
{
    return wavelengthMetres (frequencyHz, airTemperatureCelsius) * 0.5;
}

[[nodiscard]] inline constexpr double quarterWavelengthMetres (double frequencyHz, double airTemperatureCelsius = 20.0) noexcept
{
    return wavelengthMetres (frequencyHz, airTemperatureCelsius) * 0.25;
}

/** コムフィルタのキャンセル周波数 [Hz]（仕様 5.6: f = 1/(2Δt) とその奇数倍）。
    k = 1, 2, 3... に対して f_k = (2k−1) / (2·Δt) */
[[nodiscard]] inline constexpr double combFilterNullFrequency (double delayMs, int multiple) noexcept
{
    if (delayMs <= 0.0)
        return 0.0;

    return (2.0 * (double) multiple - 1.0) / (2.0 * delayMs / 1000.0);
}

/** コムフィルタのピーク周波数 [Hz]（f = k / Δt）。k = 1, 2, 3... */
[[nodiscard]] inline constexpr double combFilterPeakFrequency (double delayMs, int multiple) noexcept
{
    if (delayMs <= 0.0)
        return 0.0;

    return (double) multiple / (delayMs / 1000.0);
}

/** 距離減衰 [dB]（点音源の逆二乗則。距離が2倍で −6 dB。仕様 5.6） */
[[nodiscard]] inline double distanceAttenuationDb (double fromMetres, double toMetres) noexcept
{
    if (fromMetres <= 0.0 || toMetres <= 0.0)
        return 0.0;

    return -20.0 * std::log10 (toMetres / fromMetres);
}

/** 線音源（ラインアレイ等）の距離減衰 [dB]。距離が2倍で −3 dB。 */
[[nodiscard]] inline double lineSourceAttenuationDb (double fromMetres, double toMetres) noexcept
{
    if (fromMetres <= 0.0 || toMetres <= 0.0)
        return 0.0;

    return -10.0 * std::log10 (toMetres / fromMetres);
}

/** 面音源（壁反射等）の距離減衰 [dB]。距離に依存しない（近似）。 */
[[nodiscard]] inline double planeSourceAttenuationDb (double /*fromMetres*/, double /*toMetres*/) noexcept
{
    return 0.0;
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

/** dB の非コヒーレント合成（vector版） */
[[nodiscard]] inline double sumDecibels (const std::vector<double>& levelsDb) noexcept
{
    return sumDecibels (levelsDb.data(), (int) levelsDb.size());
}

/** dB のコヒーレント合成（同位相・同周波数の場合） */
[[nodiscard]] inline double sumDecibelsCoherent (const double* levelsDb, int count) noexcept
{
    if (levelsDb == nullptr || count <= 0)
        return -300.0;

    double sum = 0.0;

    for (int i = 0; i < count; ++i)
        sum += std::pow (10.0, levelsDb[i] / 20.0);

    return 20.0 * std::log10 (sum);
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

/** dBFS → dBu（インターフェースのヘッドルーム [dB] を指定。既定 +18 dBu = 0 dBFS） */
[[nodiscard]] inline double dbfsToDbu (double dbfs, double headroomDb = 18.0) noexcept
{
    return dbfs + headroomDb;
}

/** dBFS → dBV（インターフェースのヘッドルーム [dB] を指定。既定 +18 dBV = 0 dBFS） */
[[nodiscard]] inline double dbfsToDbv (double dbfs, double headroomDb = 18.0) noexcept
{
    return dbfs + headroomDb;
}

/** マイク感度から校正オフセットを計算。
    sensitivity: マイク感度 [mV/Pa] または [dBV/Pa]（負の値なら dBV/Pa とみなす）
    94 dB SPL = 1 Pa → 出力電圧 [V] → dBV → 校正オフセット [dB] */
[[nodiscard]] inline double calibrationOffsetFromMicSensitivity (double sensitivity) noexcept
{
    if (sensitivity == 0.0)
        return 0.0;

    // 感度が負なら dBV/Pa とみなす
    double micOutDbv;
    if (sensitivity < 0.0)
    {
        micOutDbv = sensitivity; // 既に dBV/Pa
    }
    else
    {
        // mV/Pa → V/Pa → dBV/Pa
        const auto voltsPerPa = sensitivity / 1000.0;
        micOutDbv = 20.0 * std::log10 (voltsPerPa);
    }

    // 94 dB SPL での出力 [dBV] = micOutDbv
    // 校正オフセット = 94.0 - micOutDbv（この値を足すと dB SPL になる）
    return 94.0 - micOutDbv;
}

/** クリップ検出のためのピークマージン計算。
    currentPeakDbFS: 現在のピーク [dBFS]
    返り値: 0 dBFS までの余裕 [dB]（負ならクリップ済み） */
[[nodiscard]] inline double peakMarginDb (double currentPeakDbFS) noexcept
{
    return 0.0 - currentPeakDbFS;
}

/** dB 値のクリッピング（最小・最大値で制限） */
[[nodiscard]] inline double clipDb (double value, double minDb, double maxDb) noexcept
{
    return std::clamp (value, minDb, maxDb);
}

/** BPM → 指定した拍数の時間 [ms]（仕様 5.6: テンポ⇄時間）。beats は拍数（0.5=8分音符, 1=4分音符, 2=2分音符） */
[[nodiscard]] inline constexpr double tempoToMilliseconds (double bpm, double beats = 1.0) noexcept
{
    return bpm > 0.0 ? beats * 60000.0 / bpm : 0.0;
}

/** ミリ秒 → BPM（指定分音符の長さから） */
[[nodiscard]] inline double millisecondsToTempo (double ms, double beats = 1.0) noexcept
{
    return ms > 0.0 ? beats * 60000.0 / ms : 0.0;
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

/** ゲイン [dB] → 線形振幅倍率 */
// constexpr にしない: std::pow は C++20 では constexpr でなく、
// MSVC (14.44+) / clang(libc++) ともに定義時点でエラーになる（定数式では未使用）
[[nodiscard]] inline double dbToGain (double db) noexcept
{
    return std::pow (10.0, db / 20.0);
}

/** 線形振幅倍率 → ゲイン [dB] */
[[nodiscard]] inline double gainToDb (double gain) noexcept
{
    return gain > 0.0 ? 20.0 * std::log10 (gain) : -300.0;
}

/** パワー [dB] → 線形パワー倍率 */
[[nodiscard]] inline double dbToPower (double db) noexcept
{
    return std::pow (10.0, db / 10.0);
}

/** 線形パワー倍率 → パワー [dB] */
[[nodiscard]] inline double powerToDb (double power) noexcept
{
    return power > 0.0 ? 10.0 * std::log10 (power) : -300.0;
}

/** ヘッドルーム計算（仕様 3.7 参照）。
    currentPeakDbFS: 現在のピーク [dBFS]
    targetPeakDbFS: 目標ピーク（既定 -6 dBFS）
    返り値: さらに上げられるゲイン [dB]（負なら下げる必要あり） */
[[nodiscard]] inline double headroomDb (double currentPeakDbFS, double targetPeakDbFS = -6.0) noexcept
{
    return targetPeakDbFS - currentPeakDbFS;
}

/** RMS から推定されるクレストファクター [dB]（正弦波=3.01 dB, ピンクノイズ≈6-12 dB） */
[[nodiscard]] inline double crestFactorDb (double peakDbFS, double rmsDbFS) noexcept
{
    return peakDbFS - rmsDbFS;
}

/** 圧縮量計算（仕様 3.7: レベルステップ測定用）。
    inputLevelIncreaseDb: 入力レベルを上げた量 [dB]
    splIncreaseDb: SPL が上がった量 [dB]
    返り値: 圧縮量 [dB]（0以上。1dB超で上限目安） */
[[nodiscard]] inline double compressionAmountDb (double inputLevelIncreaseDb, double splIncreaseDb) noexcept
{
    return std::max (0.0, inputLevelIncreaseDb - splIncreaseDb);
}

} // namespace eeq::util
