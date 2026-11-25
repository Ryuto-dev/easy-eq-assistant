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
    std::complex<double> h;   // 複素平均した H(f)
    float coherence = 0.0f;   // 帯域内の平均コヒーレンス
    int count = 0;            // 帯域に入った bin 数
};

/** SpectrumFrame を対数グリッドへ再サンプルしつつ、1/(2·denom) オクターブ半幅の
    複素平均（仕様 3.1 のスムージング）を適用する。
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

        std::complex<double> sumH;
        double sumCoherence = 0.0;
        int count = 0;

        for (int k = kLo; k <= kHi; ++k)
        {
            if (frame.refPowerDb[(size_t) k] < refPeakDb - floorDbBelowPeak)
                continue;

            sumH += std::complex<double> (frame.transfer[(size_t) k].real(),
                                          frame.transfer[(size_t) k].imag());
            sumCoherence += frame.coherence[(size_t) k];
            ++count;
        }

        auto& point = points[(size_t) px];
        point.freq = fc;
        point.h = count > 0 ? sumH / (double) count : std::complex<double> {};
        point.coherence = count > 0 ? (float) (sumCoherence / (double) count) : 0.0f;
        point.count = count;
    }

    return points;
}

} // namespace eeq::dsp
