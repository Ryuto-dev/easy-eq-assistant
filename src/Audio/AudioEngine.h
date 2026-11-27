#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "Core/JuceModules.h"

#include "../DSP/MeasurementEngine.h"
#include "../DSP/RtaSplAnalyzer.h"
#include "PinkNoiseGenerator.h"

namespace eeq::audio
{

// 1系統ぶんのメーター値。オーディオスレッドが書き、UIスレッドが読む。
struct MeterState
{
    std::atomic<float> peakLinear { 0.0f };
    std::atomic<float> rmsLinear { 0.0f };
    std::atomic<bool> clip { false };
};

// オーディオI/Oの中心。デバイス管理、チャンネル割当（Ref/Meas/出力）、
// ピンクノイズ出力（フェードイン/アウト付き）、レベルメーターを担う。
// 設計方針（仕様 2.）: オーディオスレッド内ではメモリ確保・ロック・ファイルI/Oをしない。
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (juce::PropertiesFile* settingsToUse);
    ~AudioEngine() override;

    /** デバイスを開く。マイク許可の確定後に呼ぶこと。
        （デバイスオープンに伴うCoreAudioの要求と許可要求が重なると、この環境では
        TCCがダイアログを出さずに自動拒否するため） */
    void openInitialDevice();

    // ---- デバイス（入力と出力は別々に選べる）----

    juce::StringArray getAvailableDeviceNames (bool wantInputNames) const;
    juce::String getSelectedInputDeviceName() const;
    juce::String getSelectedOutputDeviceName() const;
    /** 入力デバイスだけを選び直す。成功時は空文字、失敗時はエラーメッセージを返す */
    juce::String selectInputDevice (const juce::String& deviceName);
    /** 出力デバイスだけを選び直す。成功時は空文字、失敗時はエラーメッセージを返す */
    juce::String selectOutputDevice (const juce::String& deviceName);
    /** 現在の設定のまま入出力を開き直す（マイク許可を得た後などに使う） */
    void reopenDevices();
    /** サンプルレート / バッファサイズを変更する（負/0の項目は変更しない） */
    juce::String applyRateAndBuffer (double sampleRate, int bufferSize);

    juce::Array<double> getAvailableSampleRates() const;
    juce::Array<int> getAvailableBufferSizes() const;

    /** 開かれている入力/出力チャンネルの番号と表示名（"1: 名称"）を得る */
    void getOpenedInputChannels (juce::Array<int>& indicesOut, juce::StringArray& namesOut) const;
    void getOpenedOutputChannels (juce::Array<int>& indicesOut, juce::StringArray& namesOut) const;

    // デバイスが開いた/開き直されたら増える番号。UIは変化を検出してコンボを更新する
    uint32_t getDeviceGeneration() const noexcept { return deviceGeneration.load (std::memory_order_relaxed); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load (std::memory_order_relaxed); }
    int getCurrentBufferSize() const noexcept { return currentBufferSize.load (std::memory_order_relaxed); }
    juce::String getStatusText() const;

    // ---- チャンネル割当（デバイスのチャンネル番号・0開始。負は特別値）----
    // Ref は internalRefChannel を選べる（仕様 3.1 (b) 内部参照: アプリの出力信号を
    // そのまま基準信号として使う。配線が不要で、Ref入力chを消費しない）

    static constexpr int internalRefChannel = -2; // Ref = アプリの出力信号（内部参照）
    static constexpr int noChannel = -1;          // 未割当

    bool isRefInternal() const noexcept { return getRefChannel() == internalRefChannel; }
    int getRefChannel() const noexcept { return refChannel.load (std::memory_order_relaxed); }
    int getMeasChannel() const noexcept { return measChannel.load (std::memory_order_relaxed); }
    /** ルーティングマスクの最下位ビット（互換用。系統選択は setOutputRoutingMask を使う） */
    int getOutputChannel() const noexcept { return outputChannel.load (std::memory_order_relaxed); }
    void setRefChannel (int index) noexcept { refChannel.store (index, std::memory_order_relaxed); }
    void setMeasChannel (int index) noexcept { measChannel.store (index, std::memory_order_relaxed); }
    void setOutputChannel (int index) noexcept
    {
        outputChannel.store (index, std::memory_order_relaxed);
        outputMask.store (index >= 0 ? (uint64_t) 1 << index : 0, std::memory_order_relaxed);
    }

    /** 出力の系統ルーティング（仕様 3.3.4）。ビット t が立っている出力chに励起信号を出す。
        「全体（合成）」は全系統分のビットを立てる */
    void setOutputRoutingMask (uint64_t mask) noexcept { outputMask.store (mask, std::memory_order_relaxed); }
    uint64_t getOutputRoutingMask() const noexcept { return outputMask.load (std::memory_order_relaxed); }

    // ---- メーター ----

    const MeterState& getRefMeter() const noexcept { return refMeter; }
    const MeterState& getMeasMeter() const noexcept { return measMeter; }
    const MeterState& getOutMeter() const noexcept { return outMeter; }
    void clearClipLatches() noexcept;

    // ---- 励起信号（仕様 3.2 / 3.3.4）----

    enum class ExcitationSource : int
    {
        pinkNoise, // アプリ内蔵のピンクノイズ（既定）
        sweep,     // 対数スイープ（セットアップ時の測定用）
        wavFile,   // WAVファイルの再生
        external,  // アプリは出力せず取り込みだけ（本番素材での測定）
        tone1k,    // SPL校正用: 1 kHz トーン（安定させたいとき。仕様 3.4）
        bandNoise1k // SPL校正用: 1 kHz 中心の帯域制限ピンクノイズ（比較校正の既定。仕様 3.4）
    };

    /** 励起信号の種類を選ぶ。音が出ている間に変えると、オーディオスレッドが短くフェードアウト →
        切替 → フェードインで反映する（issue #104: 段差的に別の信号・別のスピーカーへ切り替えない） */
    void setExcitationSource (ExcitationSource source) noexcept
    { excitationSource.store (source, std::memory_order_relaxed); }
    ExcitationSource getExcitationSource() const noexcept
    { return excitationSource.load (std::memory_order_relaxed); }

    /** 出力をフェードイン付きで開始する */
    void startExcitation() noexcept;
    /** 出力をフェードアウト付きで停止する */
    void stopExcitation() noexcept;
    /** 互換用エイリアス（ピンクノイズ） */
    void startNoise() noexcept { startExcitation(); }
    void stopNoise() noexcept { stopExcitation(); }
    bool isNoiseAudible() const noexcept { return isExcitationAudible(); }

    void setNoiseLevelDb (float levelDb) noexcept { noiseLevelDb.store (levelDb, std::memory_order_relaxed); }
    float getNoiseLevelDb() const noexcept { return noiseLevelDb.load (std::memory_order_relaxed); }
    /** 音が出ている（フェードイン/定常）状態か */
    bool isExcitationAudible() const noexcept;

    /** 対数スイープを生成して渡す（UIスレッドで生成。長さ 2〜30 秒: 仕様 3.2） */
    void setSweep (std::vector<float> sweepSamples);
    /** WAVファイルを読み込んで励起信号にする（モノラルにダウンミックス）。失敗時 false */
    bool loadWavExcitation (const juce::File& file, juce::String& errorOut);
    /** スイープ/WAVの再生位置（0..1）。ノイズのときは意味を持たない */
    double getExcitationProgress() const noexcept;
    /** スイープ/WAVが最後まで再生されたら true を1度だけ返す */
    bool consumeExcitationFinished() noexcept
    { return excitationFinished.exchange (false, std::memory_order_relaxed); }

    // 出力の安全装置（仕様 3.7 / 6.3）: 生成サンプルをこの dBFS でハードクリップする
    void setOutputCeilingDb (float ceilingDb) noexcept
    { outputCeilingDb.store (ceilingDb, std::memory_order_relaxed); }
    float getOutputCeilingDb() const noexcept { return outputCeilingDb.load (std::memory_order_relaxed); }

    /** ユニットRMSピンクノイズの実測クレスト（ピーク/RMS）。テストで 12〜16 dB に固定。
        「RMS = 設定値」なので、設定が ceiling − この値 を超えると生成器出力が
        天井でクリップする（issue #52）。UI のレベル上限はこれで決める */
    static constexpr float excitationCrestDb = 14.3f;

    /** 励起ソースごとの RMS 正規化係数（issue #51）。
        「設定レベル = dBFS RMS（0 dBFS = 実効値 1.0）」に全ソースを揃えるための係数。
        ピンクノイズ: 生成器が RMS 1.0。スイープ/1kHzトーン: ピーク1.0・実効0.7071 の
        正弦系なので √2。WAV: ピーク正規化（= dBFS ピーク基準）。
        1kHz帯域ノイズ: フィルタ通過後の実測 RMS 0.314（tests/AudioTests.cpp が検証） */
    [[nodiscard]] static float rmsNormaliseFor (ExcitationSource source) noexcept
    {
        switch (source)
        {
            case ExcitationSource::sweep:
            case ExcitationSource::tone1k:
                return 1.41421356f;
            case ExcitationSource::bandNoise1k:
                return 3.18f;
            case ExcitationSource::wavFile:
            case ExcitationSource::pinkNoise:
            case ExcitationSource::external:
            default:
                return 1.0f;
        }
    }

    /** フェード包絡の純粋関数（issue #53 のテスト用）。
        stateInt: 0=idle, 1=fadeIn, 2=running, 3=fadeOut。
        startEnvelope はフェード開始時点の包絡値（フェードイン中の停止で
        フルレベルへ跳ねないように、現在値からフェードアウトを始める） */
    [[nodiscard]] static float fadeEnvelopeAt (int stateInt, int position, int length,
                                               float startEnvelope) noexcept
    {
        const auto fraction = juce::jlimit (0.0f, 1.0f,
                                            (float) position / (float) juce::jmax (1, length));

        switch (stateInt)
        {
            case 1:  return startEnvelope + (1.0f - startEnvelope) * fraction; // fadeIn
            case 2:  return 1.0f;                                              // running
            case 3:  return startEnvelope * (1.0f - fraction);                 // fadeOut
            default: return 0.0f;                                              // idle
        }
    }

    /** 緊急停止（Esc。仕様 6.3）: 出力をフェードアウトし、測定も止める */
    void emergencyStop() noexcept;

    // ---- SPL校正信号（仕様 3.4）----

    enum class CalibrationSignal : int { bandNoise1k, tone1k, pinkBroadband };

    /** 比較校正の信号を開始する（既定は1kHz中心の帯域制限ピンクノイズ） */
    void startCalibrationSignal (CalibrationSignal signal);

    // これを超える出力レベルは確認ダイアログを出す（仕様 3.2: 既定 −20 dBFS）
    static constexpr float outputLevelLimitDb = -20.0f;
    static constexpr double fadeOutSeconds = 0.15;
    static constexpr double uiWatchdogSilenceBudgetSeconds = 0.5;

    /** Reserve time for the fade and up to one block before the next block-start check. */
    [[nodiscard]] static constexpr std::uint64_t uiWatchdogTimeoutSamplesFor (double sampleRate,
                                                                               int bufferSize) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto budget = static_cast<std::uint64_t> (rate * uiWatchdogSilenceBudgetSeconds);
        const auto fade = static_cast<std::uint64_t> (rate * fadeOutSeconds);
        const auto block = static_cast<std::uint64_t> (bufferSize > 0 ? bufferSize : 1);
        const auto reserved = fade + block;
        return budget > reserved ? budget - reserved : 1;
    }

    // ---- RTA・SPL（仕様 3.4 / 5.4）: Meas 1chだけで常時動く ----

    eeq::dsp::RtaSplAnalyzer& getSplAnalyzer() noexcept { return splAnalyzer; }

    // ---- デバイスエラー（UIが消費する。仕様 6.1）----

    /** エラーが滞留していれば true を返して内容を out に入れ、滞留を消す */
    bool consumeDeviceError (juce::String& messageOut);

    /** フェードアウトが完了して停止したことをUIが消費する（診断ログ用） */
    bool consumeNoiseReachedIdle() noexcept
    { return noiseReachedIdle.exchange (false, std::memory_order_relaxed); }

    /** UIタイマーから呼ぶ。励起中だけ音声サンプル基準の停止期限を延長する */
    void kickUiWatchdog() noexcept;
    /** UI停止監視が励起を止めたことを一度だけ返す */
    bool consumeUiWatchdogStop() noexcept
    { return uiWatchdogStopPending.exchange (false, std::memory_order_relaxed); }

    // ---- 設定の保存（仕様 6.2 相当の軽量保存）----

    void saveSettings();

    // ---- 伝達関数測定（フェーズ2）----

    dsp::MeasurementEngine& getMeasurement() noexcept { return measurement; }

private:
    friend struct AudioEngineCallbackTestAccess;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    void restoreSavedAssignment (int numInputs, int numOutputs);

public:
    /** 終了時: 鳴っていればフェードアウトして無音になるまで（最大 timeoutMs）待つ（issue #105） */
    void fadeOutAndWait (int timeoutMs);

    /** 出力の有効chマスク（0..numOut-1 が立ったマスク）。64ch でも未定義動作にしない（issue #126） */
    [[nodiscard]] static constexpr std::uint64_t channelMaskFor (int numChannels) noexcept
    {
        return numChannels <= 0 ? 0ull : numChannels >= 64 ? ~0ull : ((1ull << numChannels) - 1ull);
    }

    /** デバイスが止まったことで励起が止められたか（1度だけ true）。UI が理由を表示する（issue #102） */
    bool consumeStoppedByDeviceRestart() noexcept
    { return stoppedByDeviceRestart.exchange (false, std::memory_order_relaxed); }

    static constexpr double switchFadeSeconds = 0.03; // 構成切替時の短いフェードアウト（issue #104）

private:
    /** スイープ/WAVバッファを公開する（メッセージスレッド専用。2世代ローテーション） */
    void publishExcitationBuffer (std::vector<float> samples);

    juce::AudioDeviceManager deviceManager;
    juce::PropertiesFile* settings = nullptr;

    // ---- 以下はオーディオスレッドと共有する状態（atomic）----

    MeterState refMeter, measMeter, outMeter;
    std::atomic<int> refChannel { -1 }, measChannel { -1 }, outputChannel { 0 };
    std::atomic<float> noiseLevelDb { -20.0f };

    enum class NoiseState : int { idle, fadeIn, running, fadeOut };
    enum class NoiseCommand : int { none, start, stop };

    // UI はコマンドだけを出す。状態遷移はオーディオスレッドが一括して管理する
    //（UI が直接 state を書くと、コールバックの書き戻しと競合するため）
    std::atomic<NoiseState> noiseState { NoiseState::idle };
    std::atomic<int> fadePosition { 0 };
    std::atomic<int> fadeInLength { 1 };
    std::atomic<int> fadeOutLength { 1 };
    // フェード開始時点の包絡（issue #53: フェードイン中に停止しても現在のレベルから
    // フェードアウトする。running からの停止は 1.0）
    std::atomic<float> fadeStartEnvelope { 0.0f };

    std::atomic<ExcitationSource> excitationSource { ExcitationSource::pinkNoise };
    std::atomic<float> outputCeilingDb { 0.0f };

    // スイープ/WAVのバッファ（issue #33: RT安全方針 / #54: 3世代ローテーション）。
    // オーディオスレッドは atomic な生ポインタを acquire 読みするだけで、
    // ロック・shared_ptrコピー・解放は一切しない。解放はメッセージスレッドだが、
    // オーディオスレッドがまだ参照している可能性があるため直前の2世代を保持する
    //（3世代ローテーション: 1ブロック内に2回差し替わっても旧バッファが生きる）
    std::shared_ptr<const std::vector<float>> ownedBuffers[3]; // メッセージスレッドだけが触る
    std::atomic<const std::vector<float>*> activeBufferPtr { nullptr };
    std::atomic<int> playbackPosition { 0 }; // オーディオスレッドだけが書く
    std::atomic<bool> excitationFinished { false };
    std::atomic<uint64_t> outputMask { 1 };

    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<int> currentBufferSize { 512 };
    std::atomic<uint32_t> deviceGeneration { 0 };

    juce::CriticalSection errorMutex;
    juce::String pendingErrorMessage;
    std::atomic<bool> hasPendingError { false };
    std::atomic<bool> noiseReachedIdle { false };
    // UI watchdogはオーディオコールバックのサンプルカウンターで進める。
    // RT側はatomic load/storeだけを使い、時計・ロック・割り当て・ログを使わない。
    std::atomic<std::uint64_t> audioSampleCounter { 0 };
    // One atomic publication: zero means stop; otherwise the upper bits hold
    // the deadline and the low bit indicates an unconsumed start.
    static constexpr std::uint64_t pendingStartBit = 1;
    std::atomic<std::uint64_t> excitationRequest { 0 };
    std::atomic<std::uint64_t> uiWatchdogTimeoutSamples
        { uiWatchdogTimeoutSamplesFor (48000.0, 512) };
    std::atomic<bool> uiWatchdogStopPending { false };
    std::atomic<bool> stoppedByDeviceRestart { false };

    // 実際に鳴らしている構成（オーディオスレッドだけが触る）。要求（excitationSource/outputMask/
    // activeBufferPtr）と違えば、フェードを挟んでから切り替える（issue #104）
    ExcitationSource appliedSource = ExcitationSource::pinkNoise;
    std::uint64_t appliedMask = 1;
    const std::vector<float>* appliedBuffer = nullptr;
    bool switchPending = false;
    bool pendingResumeAfterSwitch = false;

    PinkNoiseGenerator noise;
    juce::AudioFormatManager formatManager;
    eeq::dsp::RtaSplAnalyzer splAnalyzer;

    // 校正信号（1kHzトーン/帯域制限ピンクノイズ）の状態。オーディオスレッドだけが触る
    double tonePhase = 0.0;
    double bandNoiseCoeffs[5] = {}; // RBJ bandpass b0,b1,b2,a1,a2（サンプルレートが変わったら作り直す）
    double bandNoiseCoeffsRate = 0.0;
    double bandNoiseX1 = 0.0, bandNoiseX2 = 0.0, bandNoiseY1 = 0.0, bandNoiseY2 = 0.0;

    // 測定ストリーム（フェーズ2）: オーディオスレッド → リングバッファ → 解析スレッド
    dsp::SampleRingBuffer measurementRing;
    dsp::MeasurementEngine measurement;
    std::vector<float> refStream, measStream;

    static constexpr float defaultNoiseLevelDb = -20.0f;
    static constexpr float clipThresholdLinear = 0.98f; // 約 −0.2 dBFS（仕様 3.5 のクリップ検出）
    static constexpr double fadeInSeconds = 0.3;
};

} // namespace eeq::audio
