#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Core/JuceModules.h"

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
class MainComponent : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;

private:
    void timerCallback() override;
    void updateModalEscapeListener();
    void attachEscapeListenerRecursively (juce::Component& component);
    void showError (const juce::String& message);
    void applyMeasurementSettings();
    void applyThemeToComponents();
    void applyLookAndFeelColours();
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

    // テーマモードを他のメンバーより先に確定させる（各ウィジェットの構築時に
    // theme::get で色が焼き込まれるため。issue #9）
    struct ThemeModeInitializer
    {
        ThemeModeInitializer (juce::PropertiesFile* settings)
        {
            const auto saved = settings->getIntValue ("themeMode", (int) eeq::ui::theme::Mode::dark);
            eeq::ui::theme::setMode ((eeq::ui::theme::Mode) juce::jlimit (0, 1, saved));
        }
    };

    ThemeModeInitializer themeModeInit { appPropertiesStorage.properties.getUserSettings() };
    // 既定の LookAndFeel にする。他のメンバー（ウィジェット）より先に構築し、最後に破棄されるようここに置く
    juce::LookAndFeel_V4 lookAndFeel;
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
    std::uint32_t uiWatchdogWarningUntilMs = 0;
    std::uint32_t deviceStopNoticeUntilMs = 0;
    bool errorDialogOpen = false;
    bool wizardShown = false;
    bool initialFocusTaken = false;
    juce::TooltipWindow tooltipWindow { this };
    juce::Component::SafePointer<juce::Component> modalEscapeRoot;
    std::vector<juce::Component::SafePointer<juce::Component>> modalEscapeTargets;
    juce::Time lastAutoSave; // 定期オートセーブ用（issue #38）
    std::shared_ptr<std::atomic_bool> saveInFlight
        { std::make_shared<std::atomic_bool> (false) }; // 保存の二重実行防止（issue #58）

    static constexpr int topBarHeight = 104;
    static constexpr int bottomBarHeight = 64;
    static constexpr int measurementPanelWidth = 225;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
