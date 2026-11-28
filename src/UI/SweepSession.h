#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/Log.h"
#include "../DSP/SweepAnalysis.h"
#include "../DSP/SweepGenerator.h"

#include "Core/JuceModules.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace eeq::ui
{

// スイープ測定の流れ（2026-11）。どこから鳴らしても（下のバーの「スイープ」＋発振 / 残響ページのボタン）
// 鳴り終わったら残響ぶん待ってから、取り込んだ Ref/Meas 全体を一度に割って IR を作る。
//   - 伝達関数: MeasurementEngine の「保持した結果」として出す（伝達関数・EQ 案・IR ページがそのまま使う）
//   - 残響時間: オクターブごとの EDT / T20 / T30（残響ページ）
// 解析は別スレッド（2^20 点の FFT を何度かするので 0.5 秒ほどかかる）。
class SweepSession
{
public:
    enum class Phase { idle, playing, tail, analysing, done, failed };

    explicit SweepSession (eeq::audio::AudioEngine& engineToUse) : engine (engineToUse) {}

    ~SweepSession()
    {
        if (worker.joinable())
            worker.join();
    }

    /** 残響ページのボタン: 指定の長さのスイープを作って鳴らす */
    void startSweep (double seconds)
    {
        const auto rate = engine.getCurrentSampleRate();
        auto sweep = eeq::dsp::LogSweepGenerator::generate (rate, juce::jlimit (2.0, 30.0, seconds), 20.0, 20000.0, 1.0f);
        engine.setSweep (std::move (sweep));
        engine.startExcitation();
    }

    /** スイープの再生が最後まで行った（MainComponent が AudioEngine の通知を回す） */
    void notifyPlaybackFinished()
    {
        if (phase == Phase::playing)
        {
            phase = Phase::tail;
            tailUntilMs = juce::Time::getMillisecondCounter() + (juce::uint32) (tailSeconds * 1000.0);
        }
    }

    /** メッセージスレッドのタイマーから（30 Hz） */
    void timerTick()
    {
        using Source = eeq::audio::AudioEngine::ExcitationSource;
        const auto isSweep = engine.getExcitationSource() == Source::sweep;
        const auto audible = engine.isExcitationAudible();

        // スイープ以外に切り替えたら、保持していたスイープの結果は外す（ノイズの平均を表示に戻す）
        if (! isSweep && heldPublished)
        {
            engine.getMeasurement().setHeldResult (nullptr);
            heldPublished = false;
        }

        switch (phase)
        {
            case Phase::idle:
            case Phase::done:
            case Phase::failed:
                if (isSweep && audible)
                {
                    phase = Phase::playing;
                    sweepSamples = engine.getExcitationLengthSamples();
                    eeq::log::write ("sweep measurement: started (" + juce::String (sweepSamples) + " samples)");
                }
                break;

            case Phase::playing:
                if (! audible && engine.getExcitationProgress() < 0.98 && ! isSweep)
                    phase = Phase::idle; // 途中で止めた / 別の信号にした
                else if (! audible && engine.getExcitationProgress() < 0.98)
                {
                    // 途中で止めた（最後まで鳴らしていない）
                    if (++stoppedTicks > 6)
                    {
                        phase = Phase::idle;
                        stoppedTicks = 0;
                        eeq::log::write ("sweep measurement: cancelled (stopped before the end)");
                    }
                }
                else
                    stoppedTicks = 0;
                break;

            case Phase::tail:
                if ((juce::int32) (juce::Time::getMillisecondCounter() - tailUntilMs) >= 0)
                    beginAnalysis();
                break;

            case Phase::analysing:
                collectResult();
                break;
        }
    }

    Phase getPhase() const noexcept { return phase; }
    /** 再生中 / 残響待ちの進み具合（0..1） */
    double getProgress() const
    {
        if (phase == Phase::playing)
            return engine.getExcitationProgress() * 0.9;
        if (phase == Phase::tail)
            return 0.9 + 0.1 * juce::jlimit (0.0, 1.0, 1.0 - (double) (juce::int32) (tailUntilMs - juce::Time::getMillisecondCounter())
                                                             / (tailSeconds * 1000.0));
        return phase == Phase::analysing ? 1.0 : 0.0;
    }

    std::shared_ptr<const eeq::dsp::ReverbResult> getReverb() const { return reverb; }
    std::shared_ptr<const eeq::dsp::SweepImpulse> getImpulse() const { return impulse; }
    juce::Time getResultTime() const noexcept { return resultTime; }
    int getResultVersion() const noexcept { return resultVersion; }
    juce::String getFailureText() const { return failureText; }
    bool isShowingResult() const noexcept { return heldPublished; }

    double tailSeconds = 2.0; // 鳴り終わってから残響を待つ時間（大きいホールでも 2 秒あれば 60 dB 落ちる）

private:
    struct Output
    {
        std::shared_ptr<eeq::dsp::SweepImpulse> impulse;
        std::shared_ptr<eeq::dsp::SpectrumFrame> frame;
        std::shared_ptr<eeq::dsp::ReverbResult> reverb;
        juce::String error;
    };

    void beginAnalysis()
    {
        auto& measurement = engine.getMeasurement();
        const auto rate = measurement.getSampleRate();
        const auto pre = (int) (rate * 0.3);
        const auto total = juce::jmin (eeq::dsp::MeasurementEngine::captureCapacity,
                                       sweepSamples + (int) (rate * tailSeconds) + pre);

        std::vector<float> ref, meas;
        if (sweepSamples <= 0 || ! measurement.copyRecentSamplesExact (total, ref, meas) || (int) ref.size() < total / 2)
        {
            fail (juce::String (u8"録音が足りません（入力が止まっていないか確認してください）"));
            return;
        }

        const auto fftSize = measurement.getFftSize();
        const auto refDelay = measurement.getReferenceDelaySamples();

        if (worker.joinable())
            worker.join();

        phase = Phase::analysing;
        worker = std::thread ([this, ref = std::move (ref), meas = std::move (meas), rate, fftSize, refDelay]
        {
            Output out;
            out.impulse = std::make_shared<eeq::dsp::SweepImpulse>();

            double refEnergy = 0.0;
            for (auto v : ref)
                refEnergy += (double) v * v;

            if (refEnergy / (double) ref.size() < 1.0e-9)
                out.error = juce::String (u8"Ref が無音でした（Ref ch の割り当てを確認してください）");
            else if (! eeq::dsp::deconvolveSweep (ref, meas, rate, *out.impulse))
                out.error = juce::String (u8"インパルス応答を求められませんでした");
            else
            {
                out.frame = std::make_shared<eeq::dsp::SpectrumFrame>();
                eeq::dsp::frameFromSweep (*out.impulse, fftSize, refDelay, *out.frame);
                out.reverb = std::make_shared<eeq::dsp::ReverbResult> (eeq::dsp::analyseReverb (*out.impulse));
            }

            const std::lock_guard<std::mutex> lock (outputMutex);
            output = std::make_unique<Output> (std::move (out));
        });
    }

    void collectResult()
    {
        std::unique_ptr<Output> result;
        {
            const std::lock_guard<std::mutex> lock (outputMutex);
            result = std::move (output);
        }

        if (result == nullptr)
            return;

        if (worker.joinable())
            worker.join();

        if (result->error.isNotEmpty())
        {
            fail (result->error);
            return;
        }

        impulse = result->impulse;
        reverb = result->reverb;
        resultTime = juce::Time::getCurrentTime();
        ++resultVersion;
        engine.getMeasurement().setHeldResult (result->frame);
        heldPublished = true;
        phase = Phase::done;

        juce::String summary;
        for (const auto& band : reverb->bands)
            summary << (band.centreHz > 0 ? juce::String (juce::roundToInt (band.centreHz)) : juce::String ("all")) << "="
                    << juce::String (band.t20 > 0 ? band.t20 : band.edt, 2) << "s ";
        eeq::log::write ("sweep measurement: done, IR peak at " + juce::String (impulse->peakIndex)
                         + " samples; RT " + summary);
    }

    void fail (const juce::String& text)
    {
        failureText = text;
        phase = Phase::failed;
        eeq::log::write ("sweep measurement: failed: " + text);
    }

    eeq::audio::AudioEngine& engine;
    Phase phase = Phase::idle;
    int sweepSamples = 0, stoppedTicks = 0, resultVersion = 0;
    juce::uint32 tailUntilMs = 0;
    bool heldPublished = false;
    juce::Time resultTime;
    juce::String failureText;

    std::shared_ptr<const eeq::dsp::SweepImpulse> impulse;
    std::shared_ptr<const eeq::dsp::ReverbResult> reverb;

    std::thread worker;
    std::mutex outputMutex;
    std::unique_ptr<Output> output;
};

} // namespace eeq::ui
