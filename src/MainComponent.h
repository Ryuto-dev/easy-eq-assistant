#pragma once

#include <JuceHeader.h>

#include "Audio/AudioEngine.h"
#include "Core/Log.h"
#include "Core/ProjectStore.h"
#include "Core/SystemCorrection.h"
#include "UI/BottomBar.h"
#include "UI/Buttons.h"
#include "UI/DelayIrTab.h"
#include "UI/EQTab.h"
#include "UI/FrequencyGraphView.h"
#include "UI/LimitTestTab.h"
#include "UI/LiveModeOverlay.h"
#include "UI/MeasurementPanel.h"
#include "UI/RtaSplTab.h"
#include "UI/TopBar.h"
#include "UI/TransferFunctionTab.h"
#include "UI/UiContext.h"
#include "UI/UtilitiesTab.h"
#include "UI/VenueTab.h"

// 1ウィンドウ構成（仕様 5.1）: 上部バー / タブ領域 / 右パネル（測定設定）/ 下部バー。
// タブ: 伝達関数 / IR・遅延 / RTA・SPL / EQ案 / 上限測定 / ツール / 会場。
// 本番モード（5.4）はワンボタンで切り替わる専用画面（LiveModeOverlay）。
class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void showError (const juce::String& message);
    void applyMeasurementSettings();
    void applyThemeToComponents();
    void saveProjectIfPossible();

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
    eeq::core::ProjectStore project;
    eeq::core::SystemCorrection correction;
    eeq::ui::UiState uiState;
    eeq::ui::UiContext context { engine, *appPropertiesStorage.properties.getUserSettings(),
                                 project, correction, uiState };

    eeq::ui::TopBar topBar;
    eeq::ui::BottomBar bottomBar;
    eeq::ui::MeasurementPanel measurementPanel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    eeq::ui::TransferFunctionTab transferTab;
    eeq::ui::DelayIrTab delayTab;
    eeq::ui::RtaSplTab rtaSplTab;
    eeq::ui::EQTab eqTab;
    eeq::ui::LimitTestTab limitTab;
    eeq::ui::UtilitiesTab utilitiesTab;
    eeq::ui::VenueTab venueTab;

    eeq::ui::LiveModeOverlay liveOverlay;

    uint32_t lastSeenGeneration = 0;
    bool errorDialogOpen = false;
    bool wizardShown = false;

    static constexpr int topBarHeight = 104;
    static constexpr int bottomBarHeight = 64;
    static constexpr int measurementPanelWidth = 225;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
