#include "ImpulseResponseBuilder.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace eeq::dsp
{

ImpulseResponse buildImpulseResponse (const SpectrumFrame& frame)
{
    ImpulseResponse out;
    out.sampleRate = frame.sampleRate;

    const auto halfBins = (int) frame.transfer.size();

    if (halfBins < 2 || frame.fftSize <= 0)
        return out;

    const auto n = frame.fftSize;
    out.fftSize = n;

    if (halfBins != n / 2 + 1)
        return out; // SpectrumFrame とサイズが整合していない

    juce::dsp::FFT fft ([n] { int order = 0; while ((1 << (order + 1)) <= n) ++order; return order; }());

    // エルミート対称なフルスペクトルを組み立てて実数逆FFTへ
    std::vector<float> buffer ((size_t) n * 2, 0.0f);

    for (int k = 0; k <= n / 2; ++k)
    {
        buffer[(size_t) k * 2] = frame.transfer[(size_t) k].real();
        buffer[(size_t) k * 2 + 1] = frame.transfer[(size_t) k].imag();
    }

    for (int k = n / 2 + 1; k < n; ++k)
    {
        buffer[(size_t) k * 2] = frame.transfer[(size_t) (n - k)].real();
        buffer[(size_t) k * 2 + 1] = -frame.transfer[(size_t) (n - k)].imag();
    }

    fft.performRealOnlyInverseTransform (buffer.data());

    // JUCE の実数逆FFTは 1/N 正規化済み（H=1 が単位インパルスになる）
    out.samples.resize ((size_t) n);

    for (int i = 0; i < n; ++i)
        out.samples[(size_t) i] = buffer[(size_t) i];

    // ピーク位置（放物線補間）
    int peakIndex = 0;
    double peakAbs = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const auto a = std::abs ((double) out.samples[(size_t) i]);

        if (a > peakAbs)
        {
            peakAbs = a;
            peakIndex = i;
        }
    }

    const auto y0 = std::abs ((double) out.samples[(size_t) ((peakIndex - 1 + n) % n)]);
    const auto y1 = peakAbs;
    const auto y2 = std::abs ((double) out.samples[(size_t) ((peakIndex + 1) % n)]);
    const auto denom = y0 - 2.0 * y1 + y2;
    auto fractional = denom != 0.0 ? 0.5 * (y0 - y2) / denom : 0.0;
    fractional = juce::jlimit (-0.5, 0.5, fractional);

    out.peakTimeSamples = (double) peakIndex + fractional;
    out.peakValue = out.samples[(size_t) peakIndex];

    return out;
}

} // namespace eeq::dsp
