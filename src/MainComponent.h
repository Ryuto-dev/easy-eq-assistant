#pragma once

#include <JuceHeader.h>

#include "Audio/AudioEngine.h"
#include "UI/BottomBar.h"
#include "UI/FrequencyGraphView.h"
#include "UI/MeasurementPanel.h"
#include "UI/TopBar.h"

// 1ウィンドウ構成（仕様 5.1）: 上部バー / メイン表示（振幅・位相・コヒーレンス）/ 右パネル / 下部バー。
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
    void applyMeasurementSettings();

    // 設定ファイルの保存先を確定させてから engine に渡す必要があるため、
    // 初期化順を保証するラッパ（メンバー宣言順に構築される）
    struct AppPropertiesStorage
    {
        juce::ApplicationProperties properties;

        AppPropertiesStorage()
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "EasyEQAssistant";
            options.folderName = "EasyEQAssistant";
            options.filenameSuffix = "xml";
            options.storageFormat = juce::PropertiesFile::storeAsXML;
            properties.setStorageParameters (options);
        }
    };

    AppPropertiesStorage appPropertiesStorage;
    eeq::audio::AudioEngine engine;
    eeq::ui::TopBar topBar;
    eeq::ui::BottomBar bottomBar;
    eeq::ui::FrequencyGraphView graphView;
    eeq::ui::MeasurementPanel measurementPanel;

    eeq::dsp::SpectrumFrame latestFrame;
    uint32_t lastSeenGeneration = 0;
    bool errorDialogOpen = false;

    static constexpr int topBarHeight = 104;
    static constexpr int bottomBarHeight = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
