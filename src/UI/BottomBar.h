#pragma once

#include <JuceHeader.h>

#include "../Audio/AudioEngine.h"

namespace eeq::ui
{

// 下部バー（仕様 5.1 下部バー相当）: ピンクノイズの開始/停止と出力レベル。
// 出力レベルが −20 dBFS（engine.outputLevelLimitDb）を超えるときは確認ダイアログを出す（仕様 3.2）。
class BottomBar : public juce::Component, private juce::Slider::Listener
{
public:
    explicit BottomBar (eeq::audio::AudioEngine& engine);

    /** 30Hz程度のタイマーから呼ぶ。ボタンの表示を状態に合わせる */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;
    void confirmOverLimitIfNeeded();

    eeq::audio::AudioEngine& engine;

    juce::TextButton noiseButton;
    juce::Label levelTitle;
    juce::Slider levelSlider;

    bool overLimitConfirmed = false;
    bool dialogOpen = false;
};

} // namespace eeq::ui
