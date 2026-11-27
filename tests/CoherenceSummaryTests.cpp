#include <gtest/gtest.h>

#include "DSP/CoherenceSummary.h"

TEST (CoherenceSummary, FindsWorstCoherenceOnlyInLowBand)
{
    eeq::dsp::SpectrumFrame frame;
    frame.fftSize = 4096;
    frame.sampleRate = 48000.0;
    frame.averages = 4;
    frame.coherence.assign ((size_t) frame.fftSize / 2 + 1, 0.99f);

    const auto lowBin = (size_t) std::llround (100.0 * frame.fftSize / frame.sampleRate);
    const auto highBin = (size_t) std::llround (1000.0 * frame.fftSize / frame.sampleRate);
    frame.coherence[lowBin] = 0.42f;
    frame.coherence[highBin] = 0.01f; // 低域スキャン対象外

    const auto summary = eeq::dsp::summarizeLowBandCoherence (frame);

    ASSERT_TRUE (summary.hasData);
    EXPECT_NEAR (summary.worstCoherence, 0.42f, 1.0e-6f);
    EXPECT_NEAR (summary.worstFrequencyHz,
                 (double) lowBin * frame.sampleRate / frame.fftSize, 1.0e-9);
}

TEST (CoherenceSummary, ReturnsNoDataWhenThereAreNoBinsInRange)
{
    eeq::dsp::SpectrumFrame frame;
    frame.fftSize = 1024;
    frame.sampleRate = 8000.0;
    frame.averages = 1;
    frame.coherence.assign ((size_t) frame.fftSize / 2 + 1, 0.5f);

    const auto summary = eeq::dsp::summarizeLowBandCoherence (frame, 40.1, 40.2);

    EXPECT_FALSE (summary.hasData);
}

TEST (CoherenceSummary, RejectsUninitializedFrames)
{
    eeq::dsp::SpectrumFrame frame;
    frame.fftSize = 4096;
    frame.sampleRate = 48000.0;
    frame.coherence.assign ((size_t) frame.fftSize / 2 + 1, 0.1f);

    EXPECT_FALSE (eeq::dsp::summarizeLowBandCoherence (frame).hasData);
}
