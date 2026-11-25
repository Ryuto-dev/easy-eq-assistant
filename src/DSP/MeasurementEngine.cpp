#include "MeasurementEngine.h"

#include <algorithm>

namespace eeq::dsp
{

MeasurementEngine::MeasurementEngine (SampleRingBuffer& ringToUse)
    : juce::Thread ("EasyEQ Measurement"),
      ring (ringToUse)
{
    readRef.resize (16384);
    readMeas.resize (16384);
    startThread (juce::Thread::Priority::low);
}

MeasurementEngine::~MeasurementEngine()
{
    stopThread (1000);
}

void MeasurementEngine::startMeasurement()
{
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
