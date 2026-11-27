#pragma once

#include "Core/JuceModules.h"

#include "UiContext.h"

namespace eeq::ui
{

// 「ツール」タブ（仕様 5.6 の計算ユーティリティ8種）。
// 距離⇄遅延 / 音速 / 周波数⇄波長 / コムフィルタ / 距離減衰 / レベル換算 / テンポ⇄時間 / Q⇄帯域幅
class UtilitiesTab : public juce::Component,
                     private juce::TextEditor::Listener
{
public:
    explicit UtilitiesTab (UiContext& context);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** 30Hzタイマーから呼ぶ: 共有の気温を表示に反映する（issue #136） */
    void timerTick();

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void updateCalculations();

    UiContext& ctx;

    // 距離⇄遅延
    juce::Label distanceDelayTitle, distanceLabel, delayLabel2, distanceResultLabel, delayResultLabel;
    juce::TextEditor distanceEditor, delayEditor;

    // 音速
    juce::Label temperatureTitle, temperatureEditorLabel, soundSpeedLabel, temperatureNoteLabel;
    juce::TextEditor temperatureEditor;

    // 周波数⇄波長
    juce::Label wavelengthTitle, wavelengthFreqLabel, wavelengthResultLabel;
    juce::TextEditor wavelengthFreqEditor;

    // コムフィルタ
    juce::Label combTitle, combDelayLabel, combResultLabel;
    juce::TextEditor combDelayEditor;

    // 距離減衰
    juce::Label attenuationTitle, attenFromLabel, attenToLabel, attenResultLabel;
    juce::TextEditor attenFromEditor, attenToEditor;
    juce::Label attenFreqLabel, attenHumidityLabel;   // 空気吸収の入力（issue #36）
    juce::TextEditor attenFreqEditor, attenHumidityEditor;

    // レベル換算
    juce::Label levelTitle, voltsLabel, levelResultLabel;
    juce::TextEditor voltsEditor;

    // テンポ⇄時間
    juce::Label tempoTitle, bpmLabel, tempoResultLabel;
    juce::TextEditor bpmEditor;

    // Q⇄帯域幅
    juce::Label qbwTitle, qLabel, qResultLabel, bwLabel, bwResultLabel;
    juce::TextEditor qEditor, bwEditor;
};

} // namespace eeq::ui
