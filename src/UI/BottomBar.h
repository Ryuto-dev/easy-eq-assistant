#pragma once

#include <JuceHeader.h>

#include "../Audio/AudioEngine.h"

namespace eeq::ui
{

// 下部バー（仕様 5.1 下部バー相当）: 測定 Start/Stop、平均リセット、
// ピンクノイズの開始/停止、出力レベル、状態表示。
// 出力レベルが −20 dBFS（engine.outputLevelLimitDb）を超えるときは確認ダイアログを出す（仕様 3.2）。
class BottomBar : public juce::Component, private juce::Slider::Listener
{
public:
    explicit BottomBar (eeq::audio::AudioEngine& engine);

    /** 30Hz程度のタイマーから呼ぶ。ボタンの表示を状態に合わせる */
    void update();
    void setStatusText (const juce::String& text);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;
    void confirmOverLimitIfNeeded();

    eeq::audio::AudioEngine& engine;

    juce::TextButton measurementButton, resetButton, noiseButton;
    juce::Label levelTitle, statusLabel;
    juce::Slider levelSlider;

    bool overLimitConfirmed = false;
    bool dialogOpen = false;
};

} // namespace eeq::ui
