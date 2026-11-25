#pragma once

#include <juce_core/juce_core.h>

#include "SampleRingBuffer.h"
#include "TransferFunctionAccumulator.h"

#include <atomic>
#include <vector>

// 解析スレッド（仕様 2. の (2)）。リングバッファから Ref/Meas を吸い出し、
// Welch平均を進める。測定の Start/Stop は平均の開始/停止に対応する。

namespace eeq::dsp
{

class MeasurementEngine : private juce::Thread
{
public:
    explicit MeasurementEngine (SampleRingBuffer& ringToUse);
    ~MeasurementEngine() override;

    void startMeasurement();
    void stopMeasurement();
    bool isMeasuring() const noexcept { return measuring.load (std::memory_order_relaxed); }

    void resetAverages();

    /** FFTサイズ（点数）・サンプルレート・平均設定・窓を設定する */
    void configure (int fftSizePoints, double sampleRate, const AveragingConfig& averaging,
                    juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod);

    void copyLatestResult (SpectrumFrame& resultOut) const;
    int getAverages() const;

private:
    void run() override;

    SampleRingBuffer& ring;
    TransferFunctionAccumulator accumulator;
    std::atomic<bool> measuring { false };

    std::vector<float> readRef, readMeas;
};

} // namespace eeq::dsp
