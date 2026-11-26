#include "MeasurementEngine.h"

#include "RtaSplAnalyzer.h"

#include <algorithm>

namespace eeq::dsp
{

MeasurementEngine::MeasurementEngine (SampleRingBuffer& ringToUse)
    : juce::Thread ("EasyEQ Measurement"),
      ring (ringToUse)
{
    readRef.resize (16384);
    readMeas.resize (16384);
    captureRef.resize (captureCapacity, 0.0f);
    captureMeas.resize (captureCapacity, 0.0f);
    startThread (juce::Thread::Priority::low);
}

MeasurementEngine::~MeasurementEngine()
{
    stopThread (1000);
}

void MeasurementEngine::startMeasurement()
{
    // 前回の測定が残したアセンブリバッファ（直近fftSizeサンプル）を捨てる。
    // 残っていると再開後の最初のフレームが古い音と混ざる
    accumulator.resetAverages();
    measuring.store (true, std::memory_order_relaxed);
}

void MeasurementEngine::stopMeasurement()
{
    measuring.store (false, std::memory_order_relaxed);
}

void MeasurementEngine::resetAverages()
{
    accumulator.resetAverages();
}

void MeasurementEngine::configure (int fftSizePoints, double sampleRate, const AveragingConfig& averaging,
                                   juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod)
{
    accumulator.configure (fftSizePoints, sampleRate, averaging, windowMethod);
}

void MeasurementEngine::copyLatestResult (SpectrumFrame& resultOut) const
{
    accumulator.copyResult (resultOut);
}

int MeasurementEngine::getAverages() const
{
    return accumulator.getAverages();
}

bool MeasurementEngine::copyRecentSamples (int numSamples, std::vector<float>& refOut,
                                           std::vector<float>& measOut) const
{
    // 2の冪に制限する
    auto size = 256;

    while (size * 2 <= numSamples && size * 2 <= captureCapacity)
        size *= 2;

    const juce::ScopedLock sl (captureLock);

    if (captureFilled < size)
        return false;

    refOut.assign ((size_t) size, 0.0f);
    measOut.assign ((size_t) size, 0.0f);

    const auto start = (capturePos - size + captureCapacity) % captureCapacity;

    for (int i = 0; i < size; ++i)
    {
        const auto idx = (start + i) % captureCapacity;
        refOut[(size_t) i] = captureRef[(size_t) idx];
        measOut[(size_t) i] = captureMeas[(size_t) idx];
    }

    return true;
}

DelayEstimate MeasurementEngine::estimateCurrentDelay (bool usePhat, double minConfidence) const
{
    std::vector<float> ref, meas;

    if (! copyRecentSamples (32768, ref, meas))
        return {};

    return estimateDelay (ref.data(), meas.data(), (int) ref.size(), usePhat, minConfidence);
}

void MeasurementEngine::appendCapture (const float* ref, const float* meas, int numSamples)
{
    const juce::ScopedLock sl (captureLock);

    for (int i = 0; i < numSamples; ++i)
    {
        captureRef[(size_t) capturePos] = ref[i];
        captureMeas[(size_t) capturePos] = meas[i];
        capturePos = (capturePos + 1) % captureCapacity;
        captureFilled = std::min (captureFilled + 1, captureCapacity);
    }
}

void MeasurementEngine::run()
{
    while (! threadShouldExit())
    {
        // リングバッファを 8ms ごとに吸い出す（フレーム周期は0.34秒以上なので十分）
        int drained = 0;

        while (! threadShouldExit())
        {
            const auto available = ring.samplesAvailable();

            if (available <= 0)
                break;

            const auto toRead = std::min (available, (int) readRef.size());
            const auto got = ring.read (readRef.data(), readMeas.data(), toRead);

            if (got <= 0)
                break;

            appendCapture (readRef.data(), readMeas.data(), got);

            // RTA/SPLは測定の開始/停止にかかわらず常に入力を流す（仕様 3.4 / 5.4）
            if (auto* analyzer = splAnalyzer.load (std::memory_order_acquire); analyzer != nullptr)
                analyzer->process (readMeas.data(), got);

            if (measuring.load (std::memory_order_relaxed))
                accumulator.feedSamples (readRef.data(), readMeas.data(), got);

            drained += got;

            if (drained > (1 << 20))
                break; // 滞留しすぎたら一旦抜けてスレッド終了を確認する
        }

        juce::Thread::sleep (8);
    }
}

} // namespace eeq::dsp
