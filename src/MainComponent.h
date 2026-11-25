#pragma once

#include <JuceHeader.h>

#include "Audio/AudioEngine.h"
#include "UI/BottomBar.h"
#include "UI/TopBar.h"

// 1ウィンドウ構成（仕様 5.1）: 上部バー / メイン表示 / 下部バー。
// メイン表示はフェーズ2 で伝達関数グラフに置き換える。
class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showError (const juce::String& message);

    juce::ApplicationProperties appProperties;
    eeq::audio::AudioEngine engine;
    eeq::ui::TopBar topBar;
    eeq::ui::BottomBar bottomBar;

    juce::Label canvasTitle;
    juce::Label canvasNote;
    bool errorDialogOpen = false;

    static constexpr int topBarHeight = 104;
    static constexpr int bottomBarHeight = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
