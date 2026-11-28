#pragma once

#include <juce_core/juce_core.h>

#include "ClockDriftCompensator.h"
#include "DelayEstimator.h"
#include "MultiTimeWindow.h"
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

    /** Ref の遅延補償（issue #106）: Meas との時間合わせのため、Ref をこのサンプル数だけ遅らせてから
        Welch 平均に入れる（Smaart の Delay Finder と同じ方式）。値が変わったら平均をリセットする。
        遅延推定用のキャプチャ（copyRecentSamples）は遅らせない生の信号のまま */
    void setReferenceDelaySamples (int samples) noexcept;
    int getReferenceDelaySamples() const noexcept { return requestedRefDelay.load (std::memory_order_relaxed); }
    static constexpr int maxReferenceDelaySamples = (1 << 18) - 1; // 96 kHz で約 2.7 秒

    /** FFTサイズ（点数）・サンプルレート・平均設定・窓を設定する */
    void configure (int fftSizePoints, double sampleRate, const AveragingConfig& averaging,
                    juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod);

    void copyLatestResult (SpectrumFrame& resultOut) const;
    
    /** 解析スレッドが完了した最新フレームを取得する（非破壊的ピーク）。
        まだフレームがなければ nullptr を返す。複数タブが同時に読める。 */
    std::shared_ptr<const SpectrumFrame> getLatestFrame() const;

    /** MTW（帯域ごとに窓長を変えてつないだ）最新フレーム。MTW が無効ならメインのフレーム（ロードマップ B-2）。
        遅延を合わせてから使うこと（短い窓は時間のずれに弱い） */
    std::shared_ptr<const SpectrumFrame> getLatestMtwFrame() const;
    void setMtwEnabled (bool enabled) noexcept { mtwEnabled.store (enabled, std::memory_order_relaxed); }
    bool isMtwEnabled() const noexcept { return mtwEnabled.load (std::memory_order_relaxed); }
    
    /** 現在の平均回数を取得する（ロックフリー: issue #67）。
        copyLatestResult のように重いコピー・ロックを避け、UIからの進行確認用。 */
    int getAveragesLockFree() const noexcept
    { return accumulator.getAverages(); }

    /** 現在の平均回数を取得する（ロックフリー: issue #67）。
        MainComponent 等からの表示用。getAveragesLockFree のエイリアス。 */
    int getAverages() const noexcept { return getAveragesLockFree(); }

    /** 平均回数の上限（FIFO のときは保持フレーム数）。「N 回たまったら」の判定に使う */
    int getAverageCapacity() const noexcept { return accumulator.getAverageCapacity(); }

    /** RTA/SPL解析器を登録する（解析スレッドへ常時入力を流す。nullで停止） */
    void setSplAnalyzer (RtaSplAnalyzer* analyzer) noexcept
    { splAnalyzer.store (analyzer, std::memory_order_release); }

    /** 直近 numSamples サンプル（2の冪）の Ref/Meas をコピーする（遅延測定用: 仕様 3.3）。
        まだ溜まっていなければ false */
    bool copyRecentSamples (int numSamples, std::vector<float>& refOut, std::vector<float>& measOut) const;
    /** 2の冪に丸めず、指定サンプル数（またはリング内の量）をそのまま返す（THD録音解析用。issue #30） */
    bool copyRecentSamplesExact (int numSamples, std::vector<float>& refOut, std::vector<float>& measOut) const;

    /** 直近の入力から遅延を推定する（メッセージスレッドから呼ぶ。仕様 3.3）
        minConfidence: 有効とみなす最小 confidence（既定 5.0） */
    [[nodiscard]] DelayEstimate estimateCurrentDelay (bool usePhat, double minConfidence = 5.0) const;

    // 遅延推定・THD 用の直近バッファ。2^21 サンプル（96 kHz で約 21.8 秒）: THD のスイープ 10 秒＋残響を
    // 96 kHz でも取れる長さ（issue #114。2^20 だと 96 kHz で常に録音長不足になっていた）。両chで 16 MB
    static constexpr int captureCapacity = 1 << 21;

    /** 入出力のクロック差の補正（ClockDriftCompensator）。既定で有効 */
    void setClockDriftCompensation (bool enabled) noexcept { driftCompensationEnabled.store (enabled, std::memory_order_relaxed); }
    /** 補正中のクロック差（ppm）。補正していなければ 0 */
    float getCompensatedDriftPpm() const noexcept { return compensatedDriftPpm.load (std::memory_order_relaxed); }
    /** 推定したクロック差（ppm。補正の有無にかかわらず。未推定なら 0） */
    float getEstimatedDriftPpm() const noexcept { return estimatedDriftPpm.load (std::memory_order_relaxed); }
    /** 補正による時間合わせの取り直し回数（開始・停止・ゼロ点の取り直し。診断用） */
    int getDriftRealignments() const noexcept { return driftRealignments.load (std::memory_order_relaxed); }

private:
    void run() override;
    void appendCapture (const float* ref, const float* meas, int numSamples);
    void observeDrift();
    void resetAllAverages();

    SampleRingBuffer& ring;
    TransferFunctionAccumulator accumulator;
    std::atomic<bool> measuring { false };
    std::atomic<bool> measurementReady { false };
    std::atomic<bool> resetRequested { false };
    std::atomic<std::uint64_t> measurementGeneration { 0 };

    std::vector<float> readRef, readMeas, delayedRef;

    // Ref の遅延線（解析スレッドだけが触る）
    std::atomic<int> requestedRefDelay { 0 };
    int activeRefDelay = 0;
    std::atomic<int> accumulatorRefDelay { 0 }; // 今の平均がどの遅延補償量で溜めたものか
    // Ref の遅延線＋クロックドリフト補正（解析スレッドだけが触る）
    ClockDriftCompensator drift { maxReferenceDelaySamples + 1 };
    std::vector<float> delayedMeas;
    std::atomic<bool> driftCompensationEnabled { true };
    std::atomic<float> compensatedDriftPpm { 0.0f }, estimatedDriftPpm { 0.0f };
    std::atomic<double> analysisSampleRate { 48000.0 };
    std::atomic<int> driftRealignments { 0 };
    // 遅延推定（メッセージスレッド）向けに公開する当てはめ結果
    std::atomic<double> driftIntercept { 0.0 }, driftRateAtomic { 0.0 };
    std::atomic<bool> driftFitActive { false };
    std::atomic<std::uint64_t> driftProcessed { 0 };
    std::uint64_t nextDriftObservationAt = 0;
    std::vector<float> driftRef, driftMeas;

    std::atomic<RtaSplAnalyzer*> splAnalyzer { nullptr }; // 解析スレッドから読む（構築後に一度だけ書く）

    // 直近サンプルの循環バッファ（遅延測定用）。解析スレッドが書き、メッセージスレッドが読む
    mutable juce::CriticalSection captureLock;
    std::vector<float> captureRef, captureMeas;
    int capturePos = 0;
    int captureFilled = 0;

    // 完了フレームの公開（解析スレッド→UIスレッド、複数購読可・非破壊的）
    mutable juce::CriticalSection framePublishLock;
    std::shared_ptr<const SpectrumFrame> publishedFrame, publishedMtwFrame;

    // MTW（短い窓の平均）。configure はメッセージスレッド、feed/compose は解析スレッドなのでロックで守る
    juce::SpinLock mtwLock;
    MultiTimeWindow mtw;
    std::atomic<bool> mtwEnabled { true };
};

} // namespace eeq::dsp
