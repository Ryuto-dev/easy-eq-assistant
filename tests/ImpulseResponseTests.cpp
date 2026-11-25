#include <gtest/gtest.h>

#include "DSP/ImpulseResponseBuilder.h"

#include <cmath>

namespace
{
eeq::dsp::SpectrumFrame makeFrame (int fftSize, double sampleRate,
                                   const std::function<std::complex<double> (int)>& hOf)
{
    eeq::dsp::SpectrumFrame frame;
    frame.fftSize = fftSize;
    frame.sampleRate = sampleRate;
    frame.averages = 1;

    const auto halfBins = (size_t) fftSize / 2 + 1;
    frame.transfer.resize (halfBins);
    frame.magnitudeDb.resize (halfBins);
    frame.phaseDeg.resize (halfBins);
    frame.coherence.resize (halfBins, 1.0f);
    frame.refPowerDb.assign (halfBins, 0.0f);
    frame.measPowerDb.assign (halfBins, 0.0f);

    for (size_t k = 0; k < halfBins; ++k)
    {
        const auto h = hOf ((int) k);
        frame.transfer[k] = { (float) h.real(), (float) h.imag() };
        frame.magnitudeDb[k] = (float) (20.0 * std::log10 (std::max (std::abs (h), 1e-9)));
        frame.phaseDeg[k] = (float) (std::arg (h) * 180.0 / juce::MathConstants<double>::pi);
    }

    return frame;
}
} // namespace

TEST (ImpulseResponse, FlatResponseGivesUnitImpulseAtOrigin)
{
    const auto fftSize = 4096;
    const auto frame = makeFrame (fftSize, 48000.0, [] (int) { return std::complex<double> (1.0, 0.0); });

    const auto ir = eeq::dsp::buildImpulseResponse (frame);

    ASSERT_EQ ((int) ir.samples.size(), fftSize);
    EXPECT_NEAR (ir.peakTimeSamples, 0.0, 0.01);
    EXPECT_NEAR (ir.peakValue, 1.0, 0.001);
    EXPECT_NEAR (ir.samples[(size_t) (fftSize - 1)], 0.0, 0.001);
}

TEST (ImpulseResponse, DelayedResponsePeaksAtDelay)
{
    const auto fftSize = 4096;
    const auto delaySamples = 257.0;
    const auto sampleRate = 48000.0;

    const auto frame = makeFrame (fftSize, sampleRate, [&] (int k)
    {
        // H(f) = e^{−j2πf·D/sr}: 遅延 D の伝達関数
        const auto phase = -2.0 * juce::MathConstants<double>::pi * (double) k * delaySamples / (double) fftSize;
        return std::polar (1.0, phase);
    });

    const auto ir = eeq::dsp::buildImpulseResponse (frame);

    EXPECT_NEAR (ir.peakTimeSamples, delaySamples, 0.01);
    EXPECT_NEAR (ir.peakValue, 1.0, 0.001);
}

TEST (ImpulseResponse, GainScalesImpulse)
{
    const auto fftSize = 2048;
    const auto frame = makeFrame (fftSize, 48000.0, [] (int) { return std::complex<double> (0.5, 0.0); });

    const auto ir = eeq::dsp::buildImpulseResponse (frame);

    EXPECT_NEAR (ir.peakValue, 0.5, 0.001);
}
