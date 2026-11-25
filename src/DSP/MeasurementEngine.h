#pragma once

#include <juce_core/juce_core.h>

#include "DelayEstimator.h"
#include "SampleRingBuffer.h"
#include "TransferFunctionAccumulator.h"

#include <atomic>
#include <vector>

namespace eeq::dsp
{

class RtaSplAnalyzer;

// 解析スレッド（仕様 2. の (2)）。リングバッファから Ref/Meas を吸い出し、
// Welch平均を進める。測定の Start/Stop は平均の開始/停止に対応する。
// RTA/SPL解析器がセットされていれば、測定の開始/停止にかかわらず常に入力を流す
//（仕様 3.4 / 5.4: 本番中のSPL・RTAは Meas 1chだけで動く）。
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

    /** RTA/SPL解析器を登録する（解析スレッドへ常時入力を流す。nullで停止） */
    void setSplAnalyzer (RtaSplAnalyzer* analyzer) noexcept { splAnalyzer = analyzer; }

    /** 直近 numSamples サンプル（2の冪）の Ref/Meas をコピーする（遅延測定用: 仕様 3.3）。
        まだ溜まっていなければ false */
    bool copyRecentSamples (int numSamples, std::vector<float>& refOut, std::vector<float>& measOut) const;

    /** 直近の入力から遅延を推定する（メッセージスレッドから呼ぶ。仕様 3.3） */
    [[nodiscard]] DelayEstimate estimateCurrentDelay (bool usePhat) const;

    static constexpr int captureCapacity = 1 << 16; // 遅延推定用の直近バッファ（両ch合計で1MB）

private:
    void run() override;
    void appendCapture (const float* ref, const float* meas, int numSamples);

    SampleRingBuffer& ring;
    TransferFunctionAccumulator accumulator;
    std::atomic<bool> measuring { false };

    std::vector<float> readRef, readMeas;

    RtaSplAnalyzer* splAnalyzer = nullptr; // 解析スレッドからだけ触る

    // 直近サンプルの循環バッファ（遅延測定用）。解析スレッドが書き、メッセージスレッドが読む
    mutable juce::CriticalSection captureLock;
    std::vector<float> captureRef, captureMeas;
    int capturePos = 0;
    int captureFilled = 0;
};

} // namespace eeq::dsp
