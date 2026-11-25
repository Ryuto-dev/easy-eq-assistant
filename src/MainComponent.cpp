#include "MainComponent.h"

#include "Core/Log.h"
#include "Core/MicPermission.h"
#include "UI/Strings.h"
#include "UI/Theme.h"

// MainComponent はグローバル名前空間なので短い別名を用意する
namespace theme = eeq::ui::theme;
namespace strings = eeq::strings;

MainComponent::MainComponent()
    : engine (appPropertiesStorage.properties.getUserSettings()),
      topBar (engine),
      bottomBar (engine),
      transferTab (context),
      delayTab (context),
      rtaSplTab (context),
      eqTab (context),
      limitTab (context),
      utilitiesTab (context),
      venueTab (context),
      liveOverlay (engine)
{
    eeq::log::write ("--- Easy EQ assistant started ---");
    setOpaque (true);

    topBar.onError = [this] (const juce::String& message) { showError (message); };
    topBar.onLiveModeRequested = [this]
    {
        liveOverlay.beginLiveMode();
        liveOverlay.toFront (true);
        liveOverlay.setBounds (getLocalBounds());
    };
    topBar.onThemeToggleRequested = [this]
    {
        theme::toggle();
        applyThemeToComponents();
    };

    measurementPanel.onSettingsChanged = [this] { applyMeasurementSettings(); };

    bottomBar.onSnapshotRequested = [this] { transferTab.showSaveSnapshotDialog(); };
    bottomBar.onDelayMeasureRequested = [this]
    {
        tabs.setCurrentTabIndex (1);
        delayTab.runDelayMeasurement();
    };
    bottomBar.onCalibrationRequested = [this] { tabs.setCurrentTabIndex (2); };

    liveOverlay.onHideRequested = [this] { liveOverlay.endLiveMode(); };

    addAndMakeVisible (topBar);
    addAndMakeVisible (bottomBar);
    addAndMakeVisible (measurementPanel);
    addAndMakeVisible (tabs);
    addChildComponent (liveOverlay);

    // タブ（仕様 5.1 切替タブ）
    tabs.addTab (strings::tabTransferFunction, theme::get (theme::ColourId::windowBackground), &transferTab, false);
    tabs.addTab (strings::tabDelayIr, theme::get (theme::ColourId::windowBackground), &delayTab, false);
    tabs.addTab (strings::tabRtaSpl, theme::get (theme::ColourId::windowBackground), &rtaSplTab, false);
    tabs.addTab (strings::tabEQ, theme::get (theme::ColourId::windowBackground), &eqTab, false);
    tabs.addTab (strings::tabLimitTest, theme::get (theme::ColourId::windowBackground), &limitTab, false);
    tabs.addTab (strings::tabUtilities, theme::get (theme::ColourId::windowBackground), &utilitiesTab, false);
    tabs.addTab (strings::tabVenue, theme::get (theme::ColourId::windowBackground), &venueTab, false);
    tabs.setCurrentTabIndex (0);
    tabs.setTabBarDepth (30);
    tabs.setColour (juce::TabbedComponent::outlineColourId, theme::get (theme::ColourId::controlOutline));

    // キーボードショートカット（仕様 5.2 / 6.3）: Space=測定、S=スナップ、R=リセット、Esc=緊急停止
    setWantsKeyboardFocus (true);

    setSize (1280, 820);
    applyMeasurementSettings();

    // 前回のプロジェクトがあれば復元する（仕様 6.4）
    {
        const auto projectFile = eeq::core::ProjectStore::defaultProjectFile();

        if (projectFile.existsAsFile())
        {
            const auto error = project.loadFromFile (projectFile);
            eeq::log::write (error.isEmpty() ? "project restored: " + projectFile.getFullPathName()
                                             : "project restore FAILED: " + error);
        }
    }

    // マイク許可（TCC）。CoreAudio 経由ではダイアログが出ないことがあるため、
    // AVFoundation で明示的に確認・要求する。許可されたら入力を開き直す
    const auto micStatus = eeq::mic::getStatus();
    eeq::log::write (juce::String ("mic permission: ")
                     + (micStatus == eeq::mic::Status::granted ? "granted"
                        : micStatus == eeq::mic::Status::denied ? "DENIED"
                        : "undetermined"));

    if (micStatus == eeq::mic::Status::undetermined)
    {
        juce::Component::SafePointer<MainComponent> safeThis { this };

        eeq::mic::request ([safeThis] (bool granted)
        {
            if (safeThis == nullptr)
                return;

            eeq::log::write (granted ? "mic permission: granted" : "mic permission: DENIED by user");

            if (granted)
                safeThis->engine.reopenDevices();
        });
    }
    else if (micStatus == eeq::mic::Status::denied)
    {
        eeq::log::write ("mic permission denied: input stays silent until enabled in System Settings");

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            eeq::strings::micDeniedTitle, eeq::strings::micDeniedMessage, eeq::strings::okButton, this, nullptr);
    }

    // 初回起動ウィザード（仕様 5.5）: 機材選択→ループバック校正→ゲイン調整→測定の順に案内
    if (! appPropertiesStorage.properties.getUserSettings()->getBoolValue ("wizardShown", false))
    {
        wizardShown = true;
        appPropertiesStorage.properties.getUserSettings()->setValue ("wizardShown", true);
        appPropertiesStorage.properties.getUserSettings()->saveIfNeeded();
        // ウィザードの内容は校正・ゲインアシストの実装と合わせて RTA・SPLタブから案内する
        //（初回起動時は RTA・SPLタブを開いて案内文を出す）
        tabs.setCurrentTabIndex (2);
    }

    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.saveSettings();
    saveProjectIfPossible();
    appPropertiesStorage.properties.getUserSettings()->saveIfNeeded();
}

void MainComponent::saveProjectIfPossible()
{
    const auto error = project.saveToFile (eeq::core::ProjectStore::defaultProjectFile());

    if (error.isNotEmpty())
        eeq::log::write ("project auto-save FAILED: " + error);
}

void MainComponent::paint (juce::Graphics& g)
{
    // ダーク/ライトテーマ（仕様 5.）。グラフの下地は各ビューが描く
    g.fillAll (theme::get (theme::ColourId::windowBackground));
}

void MainComponent::applyThemeToComponents()
{
    tabs.setColour (juce::TabbedComponent::outlineColourId, theme::get (theme::ColourId::controlOutline));

    // 全タブを再描画する（色は theme::get を都度参照する実装なので、これだけで追従する）
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* component = tabs.getTabContentComponent (i))
            component->repaint();

    topBar.repaint();
    bottomBar.repaint();
    measurementPanel.repaint();
    repaint();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    topBar.setBounds (area.removeFromTop (topBarHeight));
    bottomBar.setBounds (area.removeFromBottom (bottomBarHeight));

    measurementPanel.setBounds (area.removeFromRight (measurementPanelWidth));
    tabs.setBounds (area);

    liveOverlay.setBounds (getLocalBounds());
}

void MainComponent::timerCallback()
{
    topBar.timerTick();
    bottomBar.update();

    // 測定設定のスムージングをタブへ橋渡しする（仕様 3.1: 計算値は保持、表示のみ適用）
    uiState.smoothingDenom = measurementPanel.getSmoothingOctaveDenominator();

    // 解析結果（約30fps: 仕様 2. の (3)）
    transferTab.timerTick();
    delayTab.timerTick();
    rtaSplTab.timerTick();
    eqTab.timerTick();
    limitTab.timerTick();
    venueTab.timerTick();
    liveOverlay.timerTick();

    measurementPanel.setAverageCountDisplay (engine.getMeasurement().getAverages());

    const auto status = engine.getStatusText();
    bottomBar.setStatusText (status.isNotEmpty() ? status : eeq::strings::noDeviceInfo);

    // デバイスが開き直されたら測定設定（サンプルレート）を合わせ直す
    if (engine.getDeviceGeneration() != lastSeenGeneration)
    {
        lastSeenGeneration = engine.getDeviceGeneration();
        applyMeasurementSettings();

        // RTA/SPL解析器もサンプルレートに合わせる（仕様 3.4）
        engine.getSplAnalyzer().configure (engine.getCurrentSampleRate(), 3, 0.5);
    }

    if (engine.consumeNoiseReachedIdle())
        eeq::log::write ("excitation: reached idle (fade-out completed on audio thread)");

    if (engine.consumeExcitationFinished())
        eeq::log::write ("excitation: playback reached the end");

    juce::String deviceError;

    if (engine.consumeDeviceError (deviceError))
    {
        showError (deviceError.isNotEmpty()
                       ? eeq::strings::deviceErrorMessage + "\n\n" + deviceError
                       : eeq::strings::deviceErrorMessage);
    }
}

void MainComponent::applyMeasurementSettings()
{
    engine.getMeasurement().configure (measurementPanel.getFftSize(), engine.getCurrentSampleRate(),
                                       measurementPanel.getAveragingConfig(), measurementPanel.getWindowMethod());
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // 本番モード中はEscのみ受ける（誤操作防止）
    if (liveOverlay.isVisible())
    {
        if (key.isKeyCode (juce::KeyPress::escapeKey))
        {
            engine.emergencyStop();
            return true;
        }

        return false;
    }

    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        auto& measurement = engine.getMeasurement();

        if (measurement.isMeasuring())
            measurement.stopMeasurement();
        else
            measurement.startMeasurement();

        return true;
    }

    if (key == 's' || key == 'S')
    {
        transferTab.showSaveSnapshotDialog();
        return true;
    }

    if (key == 'r' || key == 'R')
    {
        engine.getMeasurement().resetAverages();
        engine.clearClipLatches();
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        // 緊急停止（仕様 6.3）: 出力をフェードアウトして測定も止める
        engine.emergencyStop();
        return true;
    }

    return false;
}

void MainComponent::showError (const juce::String& message)
{
    if (errorDialogOpen)
        return;

    errorDialogOpen = true;
    juce::Component::SafePointer<MainComponent> safeThis { this };

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
        eeq::strings::deviceErrorTitle, message, eeq::strings::okButton, this,
        juce::ModalCallbackFunction::create ([safeThis] (int)
        {
            if (safeThis != nullptr)
                safeThis->errorDialogOpen = false;
        }));
}
