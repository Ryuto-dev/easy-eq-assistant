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
#include "UI/ApplyValues.h"
#include "UI/BottomBar.h"
#include "UI/Buttons.h"
#include "UI/DelayIrTab.h"
#include "UI/EeqLookAndFeel.h"
#include "UI/EQTab.h"
#include "UI/FrequencyGraphView.h"
#include "UI/LimitTestTab.h"
#include "UI/LiveModeOverlay.h"
#include "UI/MeasurementPanel.h"
#include "UI/RtaSplTab.h"
#include "UI/SetupPage.h"
#include "UI/SideNav.h"
#include "UI/StatusStrip.h"
#include "UI/TransferFunctionTab.h"
#include "UI/UiContext.h"
#include "UI/UtilitiesTab.h"
#include "UI/VenueTab.h"

// 1ウィンドウ構成（2026-11 リデザイン）:
//   上: ステータス帯（StatusStrip）  左: 作業ナビ（SideNav）  下: 操作バー（BottomBar）
//   中央: ページ。作業の流れ = 1 準備 / 2 測定 / 3 遅延・位相 / 4 EQ / 5 入れる値、
//         道具 = RTA・SPL / 上限測定 / 計算ツール / 会場・記録
//   右: 測定・遅延・EQ のページでだけ、測定設定（MeasurementPanel）と入れる値の要約を出す
// 本番モード（5.4）はワンボタンで切り替わる専用画面（LiveModeOverlay）。
class MainComponent : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** 画面の確認用（--screenshot）: ページ・テーマ・本番モードを切り替える */
    void prepareScreenshot (int page, bool light, bool live, bool measure = false);
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

    enum Page { pageSetup = 0, pageMeasure, pageDelay, pageEq, pageApply,
                pageRta = 10, pageLimit, pageTools, pageVenue };
    void showPage (int page);
    juce::Component* componentForPage (int page);
    bool pageHasInspector (int page) const noexcept;
    void showShortcutHelp();
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
    eeq::ui::EeqLookAndFeel lookAndFeel;
    eeq::audio::AudioEngine engine;
    eeq::core::ProjectStore project;
    eeq::core::SystemCorrection correction;
    eeq::ui::UiState uiState;
    eeq::ui::UiContext context { engine, *appPropertiesStorage.properties.getUserSettings(),
                                 project, correction, uiState };

    eeq::ui::StatusStrip statusStrip;
    eeq::ui::SideNav sideNav;
    eeq::ui::BottomBar bottomBar;
    eeq::ui::MeasurementPanel measurementPanel;
    eeq::ui::ApplySummaryPanel applySummary;

    eeq::ui::SetupPage setupPage;
    eeq::ui::ApplySheetPage applyPage;
    eeq::ui::TransferFunctionTab transferTab;
    eeq::ui::DelayIrTab delayTab;
    eeq::ui::RtaSplTab rtaSplTab;
    eeq::ui::EQTab eqTab;
    eeq::ui::LimitTestTab limitTab;
    eeq::ui::UtilitiesTab utilitiesTab;
    eeq::ui::VenueTab venueTab;

    eeq::ui::LiveModeOverlay liveOverlay;

    int currentPage = pageMeasure;
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

    static constexpr int bottomBarHeight = 60;
    static constexpr int inspectorWidth = 244;
    static constexpr int applySummaryHeight = 214;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
