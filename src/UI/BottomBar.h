#pragma once

#include "Core/JuceModules.h"

#include "../Audio/AudioEngine.h"

namespace eeq::ui
{

// 下部バー（仕様 5.1 下部バー）: Start/Stop、平均リセット、スナップショット保存、遅延測定、
// 校正、励起信号（ピンクノイズ/スイープ/WAV/外部）の開始/停止、出力レベル、状態表示。
// 出力レベルが −20 dBFS（engine.outputLevelLimitDb）を超えるときは確認ダイアログを出す（仕様 3.2）。
// 発振器アクティブ表示とショートカットヒントを常時表示（issue #141）。
class BottomBar : public juce::Component, private juce::Slider::Listener
{
public:
    explicit BottomBar (eeq::audio::AudioEngine& engine);

    /** 30Hz程度のタイマーから呼ぶ。ボタンの表示を状態に合わせる */
    void update();
    void setStatusText (const juce::String& text);

    /** 「● 出力中 −24.0 dBFS ピンクノイズ」の表示文字列（本番モードの表示とも共通: issue #141） */
    static juce::String outputStatusText (const eeq::audio::AudioEngine& engine);
    static int comboIdForSource (eeq::audio::AudioEngine::ExcitationSource source) noexcept;

    /** スナップショット保存（Sキー/ボタン共通） */
    std::function<void()> onSnapshotRequested;
    /** 遅延測定（IR・遅延タブへ移動して実行） */
    std::function<void()> onDelayMeasureRequested;
    /** 校正（RTA・SPLタブへ移動） */
    std::function<void()> onCalibrationRequested;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;
    void confirmOverLimitIfNeeded();
    void applyExcitationSource (int comboId);

    eeq::audio::AudioEngine& engine;

    juce::TextButton measurementButton, resetButton, noiseButton;
    juce::TextButton snapshotButton, delayButton, calibrationButton, stopAllButton;
    juce::ComboBox excitationCombo;
    juce::Label levelTitle, statusLabel;
    juce::Label oscillatorActiveLabel;      // 発振中インジケータ（issue #141）
    juce::Label shortcutsLabel;              // ショートカットヒント（issue #141）
    juce::Slider levelSlider;

    int lastExcitationId = 1; // 変化検出（デバイス再構築でコンボが再設定されても発火させない）
    bool overLimitConfirmed = false;
    bool dialogOpen = false;
};

} // namespace eeq::ui
