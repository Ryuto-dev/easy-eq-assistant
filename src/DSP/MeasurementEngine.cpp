#include "MeasurementEngine.h"

#include "RtaSplAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace eeq::dsp
{

MeasurementEngine::MeasurementEngine (SampleRingBuffer& ringToUse)
    : juce::Thread ("EasyEQ Measurement"),
      ring (ringToUse)
{
    readRef.resize (16384);
    readMeas.resize (16384);
    delayedRef.resize (16384);
    delayedMeas.resize (16384);
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
    // 大きなWelch平均配列の初期化は解析スレッドで行う。測定要求は即時に
    // 表示しつつ、リセット完了までは新しい平均を取り込まない。
    measurementGeneration.fetch_add (1, std::memory_order_acq_rel);
    measurementReady.store (false, std::memory_order_relaxed);
    measuring.store (true, std::memory_order_relaxed);
    resetRequested.store (true, std::memory_order_release);
}

void MeasurementEngine::stopMeasurement()
{
    measurementGeneration.fetch_add (1, std::memory_order_acq_rel);
    resetRequested.store (false, std::memory_order_release);
    measurementReady.store (false, std::memory_order_relaxed);
    measuring.store (false, std::memory_order_relaxed);
}

void MeasurementEngine::resetAverages()
{
    accumulator.resetAverages();
    const juce::SpinLock::ScopedLockType sl (mtwLock);
    mtw.resetAverages();
}

void MeasurementEngine::setReferenceDelaySamples (int samples) noexcept
{
    requestedRefDelay.store (juce::jlimit (0, maxReferenceDelaySamples, samples), std::memory_order_relaxed);
}

void MeasurementEngine::configure (int fftSizePoints, double sampleRate, const AveragingConfig& averaging,
                                   juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod)
{
    accumulator.configure (fftSizePoints, sampleRate, averaging, windowMethod);
    {
        const juce::SpinLock::ScopedLockType sl (mtwLock);
        mtw.configure (fftSizePoints, sampleRate, averaging, windowMethod);
    }
    analysisSampleRate.store (sampleRate > 0.0 ? sampleRate : 48000.0, std::memory_order_relaxed);
}

void MeasurementEngine::copyLatestResult (SpectrumFrame& resultOut) const
{
    accumulator.copyResult (resultOut);
    resultOut.refDelaySamples = (double) accumulatorRefDelay.load (std::memory_order_acquire);
}

std::shared_ptr<const SpectrumFrame> MeasurementEngine::getLatestFrame() const
{
    const juce::ScopedLock sl (framePublishLock);
    return publishedFrame;
}

std::shared_ptr<const SpectrumFrame> MeasurementEngine::getLatestMtwFrame() const
{
    const juce::ScopedLock sl (framePublishLock);
    return publishedMtwFrame != nullptr ? publishedMtwFrame : publishedFrame;
}

// getAverages はヘッダーで inline 定義済み（noexcept 付き）

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

bool MeasurementEngine::copyRecentSamplesExact (int numSamples, std::vector<float>& refOut,
                                                 std::vector<float>& measOut) const
{
    // 2の冪に丸めない版（issue #30: スイープ全長+残響分をそのまま取る）
    const juce::ScopedLock sl (captureLock);

    const auto size = juce::jlimit (0, juce::jmin (captureCapacity, numSamples), captureFilled);

    if (size <= 0)
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

    // メインローブの幅はスピーカーのインパルスの広がりを見込んで 1.5 ms
    const auto rate = analysisSampleRate.load (std::memory_order_relaxed);
    const auto guard = (int) std::lround (rate * 0.0015);
    auto estimate = estimateDelay (ref.data(), meas.data(), (int) ref.size(), usePhat, minConfidence, guard);

    // 入出力のクロックがずれている（補正中）ときは、窓の中で遅延が動いてピークが鈍る。
    // 観測から当てはめた直線で「今」の遅延を返す（窓の中心ではなく現在の値。数サンプルの差になる）
    if (driftFitActive.load (std::memory_order_acquire))
    {
        const auto now = (double) driftProcessed.load (std::memory_order_relaxed);
        estimate.delaySamples = driftIntercept.load (std::memory_order_relaxed)
                              + driftRateAtomic.load (std::memory_order_relaxed) * now;
        estimate.valid = estimate.confidence >= std::max (minConfidence, 12.0);
    }

    return estimate;
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

void MeasurementEngine::observeDrift()
{
    // 約 0.5 秒ごとに、生の Ref/Meas（直近 32768 サンプル）の遅延を観測する
    const auto now = drift.getSamplesProcessed();
    const auto rate = analysisSampleRate.load (std::memory_order_relaxed);
    constexpr int window = 32768;

    if (now < nextDriftObservationAt)
        return;

    nextDriftObservationAt = now + (std::uint64_t) (rate * 0.5);
    drift.setMinimumSpan (rate * 3.0);

    if (! driftCompensationEnabled.load (std::memory_order_relaxed))
    {
        if (drift.isActive())
            drift.resetTracking();
        compensatedDriftPpm.store (0.0f, std::memory_order_relaxed);
        return;
    }

    if (copyRecentSamples (window, driftRef, driftMeas) && (int) driftRef.size() == window)
    {
        double refEnergy = 0.0;
        for (auto v : driftRef)
            refEnergy += (double) v * v;

        if (refEnergy / window > 1.0e-8) // Ref が鳴っているときだけ（無音どうしの相関は無意味）
        {
            const auto estimate = estimateDelay (driftRef.data(), driftMeas.data(), window, true, 1.0);

            if (estimate.confidence >= 15.0)
                drift.addObservation ((double) now - window * 0.5, estimate.delaySamples);
        }
    }

    estimatedDriftPpm.store ((float) drift.getDriftPpm(), std::memory_order_relaxed);
    driftIntercept.store (drift.getIntercept(), std::memory_order_relaxed);
    driftRateAtomic.store (drift.getRate(), std::memory_order_relaxed);
    driftFitActive.store (drift.isActive(), std::memory_order_release);
    compensatedDriftPpm.store (drift.isActive() ? (float) drift.getDriftPpm() : 0.0f, std::memory_order_relaxed);
}

void MeasurementEngine::resetAllAverages()
{
    accumulator.resetAverages();
    const juce::SpinLock::ScopedLockType sl (mtwLock);
    mtw.resetAverages();
}

void MeasurementEngine::run()
{
    int samplesToSkipForMeasurement = 0;

    while (! threadShouldExit())
    {
        // Ref の遅延補償量が変わったら、古い時間合わせで溜めた平均は捨てる（issue #106）
        if (const auto wanted = requestedRefDelay.load (std::memory_order_relaxed); wanted != activeRefDelay)
        {
            activeRefDelay = wanted;
            drift.rebase();
            resetAllAverages();
            accumulatorRefDelay.store (wanted, std::memory_order_release);
        }

        if (resetRequested.exchange (false, std::memory_order_acq_rel))
        {
            const auto generation = measurementGeneration.load (std::memory_order_acquire);
            const auto queuedBeforeReset = ring.samplesAvailable();
            resetAllAverages();

            // stopMeasurement() や後続の startMeasurement() が先に来た場合、
            // 古い要求で測定状態を上書きしない。
            if (generation == measurementGeneration.load (std::memory_order_acquire)
                && measuring.load (std::memory_order_relaxed))
            {
                samplesToSkipForMeasurement = queuedBeforeReset;
                measurementReady.store (true, std::memory_order_release);
            }
        }

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

            const auto skip = std::min (samplesToSkipForMeasurement, got);
            samplesToSkipForMeasurement -= skip;

            // Ref を遅延線に通す（測定していなくても常に流し、履歴を保っておく）。
            // 遅延量を変えた直後でも、過去の Ref が残っているので即座に正しく揃う。
            // 入出力のクロックが違えば、ドリフト分も小数遅延で追従させる（ClockDriftCompensator）
            drift.process (readRef.data(), readMeas.data(), delayedRef.data(), delayedMeas.data(), got, activeRefDelay);
            driftProcessed.store (drift.getSamplesProcessed(), std::memory_order_relaxed);
            observeDrift();

            if (drift.consumeAlignmentChanged())
            {
                resetAllAverages(); // 時間合わせが変わった平均は混ぜない
                driftRealignments.store (drift.getAlignmentChangeCount(), std::memory_order_relaxed);
            }

            int framesDone = 0;
            if (measuring.load (std::memory_order_relaxed)
                && measurementReady.load (std::memory_order_acquire)
                && got > skip)
            {
                framesDone = accumulator.feedSamples (delayedRef.data() + skip,
                                                      delayedMeas.data() + skip,
                                                      got - skip);

                if (mtwEnabled.load (std::memory_order_relaxed))
                {
                    const juce::SpinLock::ScopedLockType sl (mtwLock);
                    mtw.feedSamples (delayedRef.data() + skip, delayedMeas.data() + skip, got - skip);
                }
            }

            // フレームが完了したら結果を公開（UIスレッドへ、複数購読可・非破壊的）
            if (framesDone > 0)
            {
 
                auto frame = std::make_shared<SpectrumFrame>();
                accumulator.copyResult (*frame);
                frame->refDelaySamples = (double) activeRefDelay;

                // MTW: 短い窓とつないだフレームも作る（表示・スナップショット・EQ 案用）
                std::shared_ptr<SpectrumFrame> mtwFrame;
                if (mtwEnabled.load (std::memory_order_relaxed))
                {
                    mtwFrame = std::make_shared<SpectrumFrame>();
                    const juce::SpinLock::ScopedLockType sl (mtwLock);
                    mtw.compose (*frame, *mtwFrame);
                }

                {
                    const juce::ScopedLock sl (framePublishLock);
                    publishedFrame = std::move (frame);
                    publishedMtwFrame = std::move (mtwFrame);
                }
            }

            drained += got;

            if (drained > (1 << 20))
                break; // 滞留しすぎたら一旦抜けてスレッド終了を確認する
        }

        juce::Thread::sleep (8);
    }
}

} // namespace eeq::dsp
