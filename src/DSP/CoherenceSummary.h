#pragma once

#include "TransferFunctionAccumulator.h"

namespace eeq::dsp
{

struct CoherenceSummary
{
    bool hasData = false;
    double worstFrequencyHz = 0.0;
    float worstCoherence = 1.0f;
};

/**
 * 40–500 Hz の低域コヒーレンスを集約する純粋関数。
 * 描画中に解析フレームを走査しないよう、UI timerから新フレーム時だけ呼ぶ。
 */
[[nodiscard]] inline CoherenceSummary summarizeLowBandCoherence (const SpectrumFrame& frame,
                                                                  double minFrequencyHz = 40.0,
                                                                  double maxFrequencyHz = 500.0) noexcept
{
    CoherenceSummary summary;

    if (frame.averages <= 0 || frame.fftSize <= 0 || frame.sampleRate <= 0.0
        || frame.coherence.empty() || minFrequencyHz > maxFrequencyHz)
        return summary;

    const auto binWidth = frame.sampleRate / (double) frame.fftSize;

    for (size_t k = 1; k < frame.coherence.size(); ++k)
    {
        const auto frequency = (double) k * binWidth;

        if (frequency < minFrequencyHz || frequency > maxFrequencyHz)
            continue;

        if (! summary.hasData || frame.coherence[k] < summary.worstCoherence)
        {
            summary.hasData = true;
            summary.worstFrequencyHz = frequency;
            summary.worstCoherence = frame.coherence[k];
        }
    }

    return summary;
}

} // namespace eeq::dsp
