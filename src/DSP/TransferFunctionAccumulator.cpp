#include "TransferFunctionAccumulator.h"

#include <algorithm>
#include <cmath>

namespace eeq::dsp
{

TransferFunctionAccumulator::TransferFunctionAccumulator()
{
    configure (fftSize, sampleRate, config, juce::dsp::WindowingFunction<float>::hann);
}

void TransferFunctionAccumulator::configure (int fftSizePoints, double newSampleRate, const AveragingConfig& averaging,
                                             juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod)
{
    const juce::ScopedLock sl (lock);

    auto points = juce::jlimit (256, 1 << 20, fftSizePoints);

    // FFTは2の冪のみ対応。冪でない値は直近の冪へ落とす（防御: UIは冪しか渡さない）
    {
        int pow2 = 256;

        while (pow2 * 2 <= points && pow2 < (1 << 20))
            pow2 *= 2;

        if (pow2 != points)
            points = pow2;
    }

    // 初回のconfigureは必ず初期化する（メンバーの既定fftSizeと同じ値を渡しても
    // バッファ確保がスキップされないようにする）
    const bool structural = ! configured || points != fftSize || std::abs (newSampleRate - sampleRate) > 0.5
                            || averaging.mode != config.mode || windowMethod != currentWindowMethod;
    configured = true;

    fftSize = points;
    sampleRate = newSampleRate;
    config = averaging;

    if (structural)
    {
        auto order = 0;

        while ((1 << (order + 1)) <= points)
            ++order;

        // ここで order == log2(points)（例: points=32768 → order=15）
        fft = std::make_unique<juce::dsp::FFT> (order);
        rebuildWindowData (windowMethod);
        resetAveragesInternal();
    }
}

void TransferFunctionAccumulator::rebuildWindowData (juce::dsp::WindowingFunction<float>::WindowingMethod method)
{
    currentWindowMethod = method;
    windowData.assign ((size_t) fftSize, 1.0f);

    const auto n = (double) fftSize;

    for (int i = 0; i < fftSize; ++i)
    {
        const auto t = 2.0 * juce::MathConstants<double>::pi * (double) i / n;
        double w = 1.0;

        if (method == juce::dsp::WindowingFunction<float>::hann)
        {
            w = 0.5 * (1.0 - std::cos (t));
        }
        else if (method == juce::dsp::WindowingFunction<float>::blackmanHarris)
        {
            w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2.0 * t) - 0.01168 * std::cos (3.0 * t);
        }
        else if (method == juce::dsp::WindowingFunction<float>::flatTop)
        {
            w = 0.21557895 - 0.41663158 * std::cos (t) + 0.277263158 * std::cos (2.0 * t)
                - 0.083578947 * std::cos (3.0 * t) + 0.006947368 * std::cos (4.0 * t);
        }

        windowData[(size_t) i] = (float) w;
    }
}

void TransferFunctionAccumulator::resetAverages()
{
    const juce::ScopedLock sl (lock);
    resetAveragesInternal();
}

void TransferFunctionAccumulator::resetAveragesInternal()
{
    gxx.assign ((size_t) fftSize / 2 + 1, 0.0);
    gyy.assign ((size_t) fftSize / 2 + 1, 0.0);
    gxy.assign ((size_t) fftSize / 2 + 1, {});
    assemblyRef.assign ((size_t) fftSize, 0.0f);
    assemblyMeas.assign ((size_t) fftSize, 0.0f);
    fftBufRef.assign ((size_t) fftSize * 2, 0.0f);
    fftBufMeas.assign ((size_t) fftSize * 2, 0.0f);
    assemblyPos = 0;
    samplesSinceFrame = 0;
    totalFed = 0;
    averages = 0;
    publishedAverages.store (0, std::memory_order_relaxed);

    result = {};
    result.fftSize = fftSize;
    result.sampleRate = sampleRate;
}

int TransferFunctionAccumulator::feedSamples (const float* refSamples, const float* measSamples, int numSamples)
{
    const juce::ScopedLock sl (lock);

    int framesDone = 0;
    const auto hop = fftSize / 2; // 50% オーバーラップ（仕様 3.1）

    for (int i = 0; i < numSamples; ++i)
    {
        assemblyRef[(size_t) assemblyPos] = refSamples[i];
        assemblyMeas[(size_t) assemblyPos] = measSamples[i];
        assemblyPos = (assemblyPos + 1) % fftSize;
        ++totalFed;

        // 最初の1フレームは fftSize サンプルがそろってから処理する
        // （それ以降は50%オーバーラップ = hopごとに処理: 仕様 3.1 の Welch法）
        if (++samplesSinceFrame >= hop && totalFed >= fftSize)
        {
            samplesSinceFrame = 0;
            processFrame();
            ++framesDone;
        }
    }

    return framesDone;
}

double TransferFunctionAccumulator::computeWeight() const
{
    if (config.mode == AveragingConfig::Mode::exponential)
        return 1.0 / (double) juce::jmax (1, config.maxAverages);

    const auto cappedCount = (long long) juce::jmax (1, config.maxAverages);
    return 1.0 / (double) std::min (cappedCount, averages + 1);
}

void TransferFunctionAccumulator::processFrame()
{
    const auto n = fftSize;
    const auto halfBins = (size_t) n / 2 + 1;

    std::fill (fftBufRef.begin(), fftBufRef.end(), 0.0f);
    std::fill (fftBufMeas.begin(), fftBufMeas.end(), 0.0f);

    // 循環バッファから直近 n サンプルを取り出し、窓掛けしてFFT入力へ。
    // performRealOnlyForwardTransform の契約: 生サンプルはバッファの先頭 n に連続で置く
    // （出力はインターリーブの複素ペアで bin 0..n/2 が有効）
    for (int i = 0; i < n; ++i)
    {
        auto idx = assemblyPos - n + i;

        while (idx < 0)
            idx += n;

        fftBufRef[(size_t) i] = assemblyRef[(size_t) idx] * windowData[(size_t) i];
        fftBufMeas[(size_t) i] = assemblyMeas[(size_t) idx] * windowData[(size_t) i];
    }

    fft->performRealOnlyForwardTransform (fftBufRef.data(), true);
    fft->performRealOnlyForwardTransform (fftBufMeas.data(), true);

    const auto weight = computeWeight();

    for (size_t k = 0; k < halfBins; ++k)
    {
        const std::complex<double> r (fftBufRef[k * 2], fftBufRef[k * 2 + 1]);
        const std::complex<double> m (fftBufMeas[k * 2], fftBufMeas[k * 2 + 1]);

        const auto xx = std::norm (r); // |R|²
        const auto yy = std::norm (m); // |M|²
        const auto xy = m * std::conj (r);

        if (averages == 0)
        {
            gxx[k] = xx;
            gyy[k] = yy;
            gxy[k] = xy;
        }
        else
        {
            gxx[k] += (xx - gxx[k]) * weight;
            gyy[k] += (yy - gyy[k]) * weight;
            gxy[k] += (xy - gxy[k]) * weight;
        }
    }

    ++averages;
    updateResult();
    publishedAverages.store (result.averages, std::memory_order_relaxed);
}

void TransferFunctionAccumulator::updateResult()
{
    const auto halfBins = (size_t) fftSize / 2 + 1;

    result.fftSize = fftSize;
    result.sampleRate = sampleRate;
    result.averages = (int) std::min<long long> (averages, std::numeric_limits<int>::max());

    result.transfer.resize (halfBins);
    result.magnitudeDb.resize (halfBins);
    result.phaseDeg.resize (halfBins);
    result.coherence.resize (halfBins);
    result.refPowerDb.resize (halfBins);
    result.measPowerDb.resize (halfBins);

    for (size_t k = 0; k < halfBins; ++k)
    {
        const auto xx = std::max (gxx[k], 1e-30);
        const auto yy = std::max (gyy[k], 1e-30);
        const auto h = gxy[k] / xx;

        result.transfer[k] = { (float) h.real(), (float) h.imag() };
        result.magnitudeDb[k] = (float) (20.0 * std::log10 (std::max (std::abs (h), 1e-9)));

        auto phaseDeg = std::arg (h) * 180.0 / juce::MathConstants<double>::pi;
        phaseDeg = std::remainder (phaseDeg, 360.0); // −180..180 に折り返す（仕様 3.1）
        result.phaseDeg[k] = (float) phaseDeg;

        result.coherence[k] = (float) juce::jlimit (0.0, 1.0, std::norm (gxy[k]) / (xx * yy));

        // 窓ゲインは補正していない相対値（S/N表示では差を見るため問題ない）
        result.refPowerDb[k] = (float) (10.0 * std::log10 (xx));
        result.measPowerDb[k] = (float) (10.0 * std::log10 (yy));
    }
}

void TransferFunctionAccumulator::copyResult (SpectrumFrame& resultOut) const
{
    const juce::ScopedLock sl (lock);
    resultOut = result;
}

int TransferFunctionAccumulator::getAverages() const
{
    // UI表示だけの読み出しでFFT処理中のロックを待たせない。
    return publishedAverages.load (std::memory_order_relaxed);
}

} // namespace eeq::dsp
