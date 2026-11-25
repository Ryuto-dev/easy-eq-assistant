#pragma once

#include "TransferFunctionAccumulator.h"

#include <cmath>
#include <complex>
#include <vector>

namespace eeq::dsp
{

// 対数グリッド上の1点ぶんのスムージング結果。
struct GridPoint
{
    double freq = 0.0;
    std::complex<double> h;   // 振幅（パワー平均）と位相（アンラップ平均）から再構成した複素H
    float coherence = 0.0f;   // 帯域内の平均コヒーレンス
    int count = 0;            // 帯域に入った bin 数
    float magnitudeDb = -300.0f; // 帯域内のパワー平均振幅 (dB)。遅延があっても崩れない
    float phaseDeg = 0.0f;       // 帯域内のアンラップ平均位相 (deg)
};

/** SpectrumFrame を対数グリッドへ再サンプルしつつ、1/(2·denom) オクターブ半幅の
    スムージング（仕様 3.1）を適用する。

    - 振幅は帯域内の「パワー平均」|H|² を平均する。遅延を含む測定では帯域内で位相が
      回転するため、複素平均すると振幅が打ち消されて崩れる（従来の実装の欠陥）。
      パワー平均なら遅延の有無に依存しない。
    - 位相は帯域内の bin を順にアンラップしてから平均する。こちらも複素平均の位相消滅を避ける。
      結果は連続的なアンラップ位相（表示側で −180..180 に折り返して見せる）。

    Ref パワーがピークより floorDbBelowPeak 以上低い bin は H1 が不定のため除外する
    （計算値は呼び出し元が保持し、表示/提案にのみ使う。仕様 3.1）。

    octaveDenom: 0 = スムージングなし（bin をそのままグリッドへ） */
[[nodiscard]] inline std::vector<GridPoint> smoothToLogGrid (const SpectrumFrame& frame, int octaveDenom,
                                                             double fMin, double fMax, int numPoints,
                                                             float floorDbBelowPeak = 80.0f)
{
    std::vector<GridPoint> points;
    points.resize ((size_t) juce::jmax (1, numPoints));

    if (frame.fftSize == 0 || frame.sampleRate <= 0.0 || frame.magnitudeDb.empty()
        || frame.transfer.size() != frame.magnitudeDb.size())
        return points;

    const auto nBins = (int) frame.magnitudeDb.size();
    const auto effectiveMax = std::min (fMax, frame.sampleRate * 0.5);

    if (! (effectiveMax > fMin))
        return points;

    const auto binWidth = frame.sampleRate / (double) frame.fftSize;
    const auto factor = octaveDenom > 0 ? std::pow (2.0, 1.0 / (2.0 * octaveDenom)) : 1.0;

    float refPeakDb = -300.0f;

    for (const auto level : frame.refPowerDb)
        refPeakDb = std::max (refPeakDb, level);

    // 帯域をまたいで連続的なアンラップ位相を作るための走査状態
    double runningPhaseDeg = 0.0;
    bool haveRunningPhase = false;

    const auto w = (int) points.size();

    for (int px = 0; px < w; ++px)
    {
        const auto f0 = fMin * std::pow (effectiveMax / fMin, px / (double) w);
        const auto f1 = fMin * std::pow (effectiveMax / fMin, (px + 1) / (double) w);
        const auto fc = std::sqrt (f0 * f1);

        const auto lo = octaveDenom > 0 ? fc / factor : f0;
        const auto hi = octaveDenom > 0 ? fc * factor : f1;

        auto kLo = juce::jlimit (1, nBins - 1, (int) std::ceil (lo / binWidth));
        auto kHi = juce::jlimit (1, nBins - 1, std::max (kLo, (int) std::floor (hi / binWidth)));

        double sumPower = 0.0;       // Σ|H|²
        double sumSin = 0.0, sumCos = 0.0; // アンラップ位相の円平均用
        double sumCoherence = 0.0;
        int count = 0;

        for (int k = kLo; k <= kHi; ++k)
        {
            if (frame.refPowerDb[(size_t) k] < refPeakDb - floorDbBelowPeak)
                continue;

            const auto h = std::complex<double> (frame.transfer[(size_t) k].real(),
                                                 frame.transfer[(size_t) k].imag());
            const auto phaseDeg = std::arg (h) * 180.0 / juce::MathConstants<double>::pi;

            // 前の bin からの位相を連続になるようアンラップする（帯域をまたいで継続）
            auto unwrapped = phaseDeg;

            if (haveRunningPhase)
            {
                while (unwrapped - runningPhaseDeg > 180.0)
                    unwrapped -= 360.0;

                while (unwrapped - runningPhaseDeg < -180.0)
                    unwrapped += 360.0;
            }

            runningPhaseDeg = unwrapped;
            haveRunningPhase = true;

            sumPower += std::norm (h);
            sumSin += std::sin (unwrapped * juce::MathConstants<double>::pi / 180.0);
            sumCos += std::cos (unwrapped * juce::MathConstants<double>::pi / 180.0);
            sumCoherence += frame.coherence[(size_t) k];
            ++count;
        }

        auto& point = points[(size_t) px];
        point.freq = fc;
        point.count = count;
        point.coherence = count > 0 ? (float) (sumCoherence / (double) count) : 0.0f;

        if (count > 0)
        {
            // 振幅: パワー平均（遅延に依存しない）
            point.magnitudeDb = (float) (10.0 * std::log10 (std::max (sumPower / (double) count, 1.0e-30)));

            // 位相: アンラップ位相の円平均（帯域内で位相が回っていても消滅しない）
            point.phaseDeg = (float) (std::atan2 (sumSin, sumCos) * 180.0 / juce::MathConstants<double>::pi);

            point.h = std::polar (std::pow (10.0, point.magnitudeDb / 20.0),
                                  point.phaseDeg * juce::MathConstants<double>::pi / 180.0);
        }
        else
        {
            point.magnitudeDb = -300.0f;
            point.phaseDeg = 0.0f;
            point.h = {};
        }
    }

    return points;
}

} // namespace eeq::dsp
