#pragma once

#include <atomic>

#include <JuceHeader.h>

#include "../DSP/MeasurementEngine.h"
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

    // ---- デバイス（入力と出力は別々に選べる）----

    juce::StringArray getAvailableDeviceNames (bool wantInputNames) const;
    juce::String getSelectedInputDeviceName() const;
    juce::String getSelectedOutputDeviceName() const;
    /** 入力デバイスだけを選び直す。成功時は空文字、失敗時はエラーメッセージを返す */
    juce::String selectInputDevice (const juce::String& deviceName);
    /** 出力デバイスだけを選び直す。成功時は空文字、失敗時はエラーメッセージを返す */
    juce::String selectOutputDevice (const juce::String& deviceName);
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
    int getOutputChannel() const noexcept { return outputChannel.load (std::memory_order_relaxed); }
    void setRefChannel (int index) noexcept { refChannel.store (index, std::memory_order_relaxed); }
    void setMeasChannel (int index) noexcept { measChannel.store (index, std::memory_order_relaxed); }
    void setOutputChannel (int index) noexcept { outputChannel.store (index, std::memory_order_relaxed); }

    // ---- メーター ----

    const MeterState& getRefMeter() const noexcept { return refMeter; }
    const MeterState& getMeasMeter() const noexcept { return measMeter; }
    const MeterState& getOutMeter() const noexcept { return outMeter; }
    void clearClipLatches() noexcept;

    // ---- ピンクノイズ（仕様 3.2）----

    /** 出力をフェードイン付きで開始する */
    void startNoise() noexcept;
    /** 出力をフェードアウト付きで停止する */
    void stopNoise() noexcept;
    void setNoiseLevelDb (float levelDb) noexcept { noiseLevelDb.store (levelDb, std::memory_order_relaxed); }
    float getNoiseLevelDb() const noexcept { return noiseLevelDb.load (std::memory_order_relaxed); }
    /** 音が出ている（フェードイン/定常）状態か */
    bool isNoiseAudible() const noexcept;

    // これを超える出力レベルは確認ダイアログを出す（仕様 3.2: 既定 −20 dBFS）
    static constexpr float outputLevelLimitDb = -20.0f;

    // ---- デバイスエラー（UIが消費する。仕様 6.1）----

    /** エラーが滞留していれば true を返して内容を out に入れ、滞留を消す */
    bool consumeDeviceError (juce::String& messageOut);

    // ---- 設定の保存（仕様 6.2 相当の軽量保存）----

    void saveSettings();

    // ---- 伝達関数測定（フェーズ2）----

    dsp::MeasurementEngine& getMeasurement() noexcept { return measurement; }

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

    void openInitialDevice();
    void restoreSavedAssignment (int numInputs, int numOutputs);

    juce::AudioDeviceManager deviceManager;
    juce::PropertiesFile* settings = nullptr;

    // ---- 以下はオーディオスレッドと共有する状態（atomic）----

    MeterState refMeter, measMeter, outMeter;
    std::atomic<int> refChannel { -1 }, measChannel { -1 }, outputChannel { 0 };
    std::atomic<float> noiseLevelDb { -20.0f };

    enum class NoiseState : int { idle, fadeIn, running, fadeOut };
    std::atomic<NoiseState> noiseState { NoiseState::idle };
    std::atomic<int> fadePosition { 0 };
    std::atomic<int> fadeInLength { 1 };
    std::atomic<int> fadeOutLength { 1 };

    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<int> currentBufferSize { 512 };
    std::atomic<uint32_t> deviceGeneration { 0 };

    juce::CriticalSection errorMutex;
    juce::String pendingErrorMessage;
    std::atomic<bool> hasPendingError { false };

    PinkNoiseGenerator noise;

    // 測定ストリーム（フェーズ2）: オーディオスレッド → リングバッファ → 解析スレッド
    dsp::SampleRingBuffer measurementRing;
    dsp::MeasurementEngine measurement;
    std::vector<float> refStream, measStream;

    static constexpr float defaultNoiseLevelDb = -20.0f;
    static constexpr float clipThresholdLinear = 0.98f; // 約 −0.2 dBFS（仕様 3.5 のクリップ検出）
    static constexpr double fadeInSeconds = 0.3;
    static constexpr double fadeOutSeconds = 0.15;
};

} // namespace eeq::audio
