#include <gtest/gtest.h>

#include "DSP/ImpulseResponseBuilder.h"

#include <cmath>
#include <random>

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

// #118: IR のピークも符号付きの値で補間する（真の小数部 0.1 / 0.3 / 0.7）
TEST (ImpulseResponse, FractionalDelayPeakIsInterpolatedOnSignedValues)
{
    const auto fftSize = 4096;

    for (const auto fraction : { 0.1, 0.3, 0.7 })
    {
        const auto delaySamples = 300.0 + fraction;
        const auto frame = makeFrame (fftSize, 48000.0, [&] (int k)
        {
            if (k == fftSize / 2)
                return std::complex<double> (0.0, 0.0); // Nyquist は実数でしか表せないので 0
            const auto phase = -2.0 * juce::MathConstants<double>::pi * (double) k * delaySamples / (double) fftSize;
            return std::polar (1.0, phase);
        });

        const auto ir = eeq::dsp::buildImpulseResponse (frame);
        EXPECT_NEAR (ir.peakTimeSamples, delaySamples, 0.02) << "fraction=" << fraction;
    }
}

// #115: Ref にエネルギーが無い bin（帯域制限された音楽の 16 kHz 以上など）の巨大な H1 を
// IFFT に入れない。旧実装では雑音の最大値が真のピークを上回り、到達時間を誤検出した
TEST (ImpulseResponse, BinsWithoutReferenceEnergyAreExcluded)
{
    const auto fftSize = 32768;
    const auto delaySamples = 1000.0;
    const auto validBins = fftSize / 3; // 帯域の 2/3 だけ Ref がある
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> phaseDist (-juce::MathConstants<double>::pi, juce::MathConstants<double>::pi);

    auto frame = makeFrame (fftSize, 48000.0, [&] (int k)
    {
        if (k < validBins)
        {
            const auto phase = -2.0 * juce::MathConstants<double>::pi * (double) k * delaySamples / (double) fftSize;
            return std::polar (1.0, phase);
        }

        return std::polar (125.0, phaseDist (rng)); // +42 dB のランダム位相
    });

    for (size_t k = (size_t) validBins; k < frame.refPowerDb.size(); ++k)
        frame.refPowerDb[k] = -150.0f; // Ref のピーク（0 dB）より 150 dB 低い

    const auto ir = eeq::dsp::buildImpulseResponse (frame);

    // 帯域制限された単位インパルス: ピーク振幅は有効 bin の割合 ≈ 2/3、位置は 1000
    EXPECT_NEAR (ir.peakTimeSamples, delaySamples, 0.05);
    EXPECT_NEAR (ir.peakValue, 2.0 / 3.0, 0.01);

    // γ² が低い bin（平均2回以上のとき）も同様に除外する
    auto lowCoherence = makeFrame (fftSize, 48000.0, [&] (int k)
    {
        if (k < validBins)
        {
            const auto phase = -2.0 * juce::MathConstants<double>::pi * (double) k * delaySamples / (double) fftSize;
            return std::polar (1.0, phase);
        }

        return std::polar (125.0, phaseDist (rng));
    });
    lowCoherence.averages = 16;

    for (size_t k = (size_t) validBins; k < lowCoherence.coherence.size(); ++k)
        lowCoherence.coherence[k] = 0.02f;

    const auto irCoh = eeq::dsp::buildImpulseResponse (lowCoherence);
    EXPECT_NEAR (irCoh.peakTimeSamples, delaySamples, 0.05);
    EXPECT_NEAR (irCoh.peakValue, 2.0 / 3.0, 0.01);
}
