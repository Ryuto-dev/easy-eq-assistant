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

    // アプリ全体の LookAndFeel（ダイアログ・メニューもテーマに追従させる: issue #132）
    applyLookAndFeelColours();
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

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

        // テーマ選択を保存する（issue #9: 再起動後もライト/ダークを維持）
        auto* settings = appPropertiesStorage.properties.getUserSettings();
        settings->setValue ("themeMode", (int) theme::mode());
        settings->saveIfNeeded();
    };

    measurementPanel.setSettings (appPropertiesStorage.properties.getUserSettings());
    transferTab.setSettings (appPropertiesStorage.properties.getUserSettings());
    liveOverlay.setSettings (appPropertiesStorage.properties.getUserSettings());
    measurementPanel.onSettingsChanged = [this] { applyMeasurementSettings(); };

    bottomBar.onSnapshotRequested = [this] { transferTab.showSaveSnapshotDialog(); };
    bottomBar.onDelayMeasureRequested = [this]
    {
        tabs.setCurrentTabIndex (1);
        delayTab.runDelayMeasurement (false);
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

    // キーボードショートカット（仕様 5.2 / 6.3）:
    // Space=測定、N=発振器、D=遅延検出、S=スナップ、R=リセット、Esc=緊急停止
    setWantsKeyboardFocus (true);

    // ツールチップ（issue #149）。TooltipWindow がないと setTooltip は表示されない
    tooltipWindow.setMillisecondsBeforeTipAppears (600);

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

            // ループバック校正（システム補正）はプロジェクトからは復元しない（issue #121）。
            // 補正はオーディオインターフェースの特性なので、正本はアプリ設定（下）に一本化する。
            // プロジェクトから戻すと、クリアした補正が再起動で復活していた

            // タブ類に一覧の張り直しを促す（スナップショット等の生ポインタも入れ替わる）
            uiState.snapshotVersion.fetch_add (1);
            uiState.eqVersion.fetch_add (1);
        }
    }

    // ループバック補正（システム補正）を復元する（issue #21）。
    // fftSize/サンプルレートが現在と違う場合は matches() が適用を止めるので安全
    {
        const auto* settings = appPropertiesStorage.properties.getUserSettings();
        const auto saved = settings->getValue ("loopbackCorrection");

        if (saved.isNotEmpty())
        {
            if (context.correction.fromVar (juce::JSON::parse (saved)))
            {
                eeq::log::write (juce::String ("loopback correction restored (measured on device: ")
                                     + settings->getValue ("loopbackDevice") + ")");
                delayTab.refreshLoopbackStatus();
            }
            else
            {
                // 復元できなかったことを利用者に知らせる（issue #89: 気づかないまま補正なしで測っていた）
                eeq::log::write ("loopback correction restore FAILED (saved data was invalid)");
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    strings::loopbackSection, strings::loopbackRestoreFailed, strings::okButton, this, nullptr);
            }
        }
    }

    // マイク許可（TCC）。**デバイスを開く前に**許可を確定させる:
    // CoreAudioの入力オープンと許可要求が重なると、この環境ではTCCが
    // ダイアログを出さずに自動拒否する（probe.app では正常にダイアログが出ることを確認済み）
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
            safeThis->engine.openInitialDevice();
        });
    }
    else
    {
        if (micStatus == eeq::mic::Status::denied)
        {
            eeq::log::write ("mic permission denied: input stays silent until enabled in System Settings");

            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                eeq::strings::micDeniedTitle, eeq::strings::micDeniedMessage, eeq::strings::okButton, this, nullptr);
        }

        engine.openInitialDevice();
    }

    // 初回起動ウィザード（仕様 5.5）: 機材選択→ループバック校正→ゲイン調整→測定の順に案内
    if (! appPropertiesStorage.properties.getUserSettings()->getBoolValue ("wizardShown", false))
    {
        wizardShown = true;
        appPropertiesStorage.properties.getUserSettings()->setValue ("wizardShown", true);
        appPropertiesStorage.properties.getUserSettings()->saveIfNeeded();

        const auto guide = juce::String (strings::wizardTitle) + "\n\n"
                         + strings::wizardStep1 + "\n"
                         + strings::wizardStep2 + "\n"
                         + strings::wizardStep3 + "\n"
                         + strings::wizardStep4 + "\n\n"
                         + strings::shortcutHint + " / " + strings::emergencyStopHint + "\n"
                         + strings::wizardFootnote;

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            strings::wizardTitle, guide, strings::wizardClose, this, nullptr);
    }

    startTimerHz (30); // 解析は別スレッド、UI操作と表示更新は30Hzで維持

    lastAutoSave = juce::Time::getCurrentTime();
}

MainComponent::~MainComponent()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

    for (auto& target : modalEscapeTargets)
        if (target != nullptr)
            target->removeKeyListener (this);

    modalEscapeTargets.clear();
    modalEscapeRoot = nullptr;

    // 鳴らしたまま閉じてもポップ音を出さない（issue #105）: フェードアウトが終わるまで待ってから閉じる
    engine.fadeOutAndWait (400);
    engine.saveSettings();
    // 最終保存は同期的に行う（自動保存がワーカースレッドのまま終了すると、
    // 終了処理に書き込みが殺されて失われる可能性があるため）。
    // 進行中の自動保存（古い内容）が最終保存のあとに書き終わって上書きしないよう、先に完了を待つ（issue #122）
    for (int i = 0; i < 300 && saveInFlight->load(); ++i)
        juce::Thread::sleep (10);

    {
        const auto error = project.saveToFile (eeq::core::ProjectStore::defaultProjectFile());

        if (error.isNotEmpty())
            eeq::log::write ("project save FAILED: " + error);
    }
    appPropertiesStorage.properties.getUserSettings()->saveIfNeeded();
}

void MainComponent::saveProjectIfPossible()
{
    // 自動保存はワーカースレッドへ移す（issue #58）: スナップショットが増えると
    // JSON化が数百 ms になり、60秒ごとのタイマーで UI が固まるため。
    // メッセージスレッドではデータのコピーだけを行い（数 ms）、JSON 化と書き込みを
    // 別スレッドで行う。inFlight フラグは shared_ptr で持ち、保存中にコンポーネントが
    // 破棄されてもフラグの開放が安全に行われるようにする
    if (saveInFlight->exchange (true))
        return;

    auto venues = project.venues(); // 深いコピー（メッセージスレッドで安全に取る）
    const auto currentIndex = project.getCurrentVenueIndex();
    const auto file = eeq::core::ProjectStore::defaultProjectFile();
    auto inFlight = saveInFlight;

    juce::Thread::launch ([inFlight, venues = std::move (venues), currentIndex, file]
    {
        const auto error = eeq::core::ProjectStore::saveVenuesToFile (venues, currentIndex, file);

        if (error.isNotEmpty())
            eeq::log::write ("project auto-save FAILED: " + error);

        // 次の保存を許可する。終了処理が完了を待てるよう、ここで直接下ろす（atomic なので安全）
        inFlight->store (false);
    });
}

void MainComponent::paint (juce::Graphics& g)
{
    // ダーク/ライトテーマ（仕様 5.）。グラフの下地は各ビューが描く
    g.fillAll (theme::get (theme::ColourId::windowBackground));
}

void MainComponent::applyLookAndFeelColours()
{
    // PopupMenu・AlertWindow・CallOutBox などは LookAndFeel の配色で描かれる（issue #132:
    // 既定の V4 のままだとライトテーマでもダイアログやメニューが暗い配色のままだった）
    using CS = juce::LookAndFeel_V4::ColourScheme;
    lookAndFeel.setColourScheme (CS (theme::get (theme::ColourId::windowBackground),    // windowBackground
                                     theme::get (theme::ColourId::panelBackground),     // widgetBackground
                                     theme::get (theme::ColourId::panelBackground),     // menuBackground
                                     theme::get (theme::ColourId::controlOutline),      // outline
                                     theme::get (theme::ColourId::text),                // defaultText
                                     theme::get (theme::ColourId::control),             // defaultFill
                                     theme::get (theme::ColourId::text),                // highlightedText
                                     theme::get (theme::ColourId::accent).withAlpha (0.35f), // highlightedFill
                                     theme::get (theme::ColourId::text)));              // menuText
    lookAndFeel.setColour (juce::TabbedButtonBar::tabTextColourId, theme::get (theme::ColourId::textDim));
    lookAndFeel.setColour (juce::TabbedButtonBar::frontTextColourId, theme::get (theme::ColourId::text));
    lookAndFeel.setColour (juce::TooltipWindow::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    lookAndFeel.setColour (juce::TooltipWindow::textColourId, theme::get (theme::ColourId::text));
    lookAndFeel.setColour (juce::TooltipWindow::outlineColourId, theme::get (theme::ColourId::controlOutline));
    lookAndFeel.setColour (juce::AlertWindow::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    lookAndFeel.setColour (juce::AlertWindow::textColourId, theme::get (theme::ColourId::text));
    lookAndFeel.setColour (juce::AlertWindow::outlineColourId, theme::get (theme::ColourId::controlOutline));
    lookAndFeel.setColour (juce::PopupMenu::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    lookAndFeel.setColour (juce::PopupMenu::textColourId, theme::get (theme::ColourId::text));
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::get (theme::ColourId::accent).withAlpha (0.35f));
    lookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId, theme::get (theme::ColourId::text));
}

void MainComponent::applyThemeToComponents()
{
    applyLookAndFeelColours();
    // 生成時に各ウィジェットへ焼き込まれた色を、現在のテーマで塗り直す（issue #9/#50）
    eeq::ui::restyleTree (this);

    tabs.setColour (juce::TabbedComponent::outlineColourId, theme::get (theme::ColourId::controlOutline));

    // addTab 時に焼き込んだタブ背景色も張り直す
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        tabs.setTabBackgroundColour (i, theme::get (theme::ColourId::windowBackground));

    // 全タブを再描画する
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

void MainComponent::attachEscapeListenerRecursively (juce::Component& component)
{
    component.addKeyListener (this);
    modalEscapeTargets.emplace_back (&component);

    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = component.getChildComponent (i))
            attachEscapeListenerRecursively (*child);
}

void MainComponent::updateModalEscapeListener()
{
    auto* modal = juce::ModalComponentManager::getInstance()->getModalComponent (0);

    if (modal == modalEscapeRoot.getComponent())
        return;

    for (auto& target : modalEscapeTargets)
        if (target != nullptr)
            target->removeKeyListener (this);

    modalEscapeTargets.clear();
    modalEscapeRoot = modal;

    if (modal != nullptr)
        attachEscapeListenerRecursively (*modal);
}

void MainComponent::timerCallback()
{
    engine.kickUiWatchdog();

    // 起動直後にキーボードフォーカスを取る（クリックしなくても Space/Esc が効くように: issue #140）
    if (! initialFocusTaken && isShowing())
    {
        initialFocusTaken = true;
        grabKeyboardFocus();
    }

    updateModalEscapeListener();
    const auto nowMs = juce::Time::getMillisecondCounter();

    if (engine.consumeUiWatchdogStop())
    {
        uiWatchdogWarningUntilMs = nowMs + 3000u;
        eeq::log::write ("UI watchdog: timer stalled; excitation and measurement were stopped");
    }

    // 定期オートセーブ（issue #38）: 60秒ごとに last_project.eea へ書く。
    // クラッシュ・強制終了・電源断でも直近60秒以内の状態が残る。
    // 書き込みはアトミック（ProjectStore::saveToFile）なので、
    // 途中で落ちても既存ファイルは壊れない
    if ((juce::Time::getCurrentTime() - lastAutoSave).inSeconds() >= 60)
    {
        lastAutoSave = juce::Time::getCurrentTime();
        saveProjectIfPossible();
    }

    // 適用中の遅延で Ref を時間合わせする（issue #106: 位相表示だけでなく H1 とコヒーレンスの偏りを無くす）
    {
        const auto applied = uiState.suspendReferenceDelay.load() ? 0.0 : uiState.appliedDelaySamples.load();
        const auto wanted = juce::jlimit (0, eeq::dsp::MeasurementEngine::maxReferenceDelaySamples,
                                          (int) std::lround (std::isfinite (applied) ? applied : 0.0));

        if (wanted != engine.getMeasurement().getReferenceDelaySamples())
        {
            engine.getMeasurement().setReferenceDelaySamples (wanted);
            eeq::log::write ("measurement: reference delay compensation = " + juce::String (wanted) + " samples");
        }
    }

    topBar.timerTick();
    bottomBar.update();

    // 測定設定のスムージングをタブへ橋渡しする（仕様 3.1: 計算値は保持、表示のみ適用）
    uiState.smoothingDenom = measurementPanel.getSmoothingOctaveDenominator();

    // 解析結果（約30fps: 仕様 2. の (3)）。
    // 非表示タブは重い再計算（IR再構築・スムージング等）を止める（issue #37）:
    // 再表示後の最初のティックで自然に最新化される。ただし実行中のリミットテストは
    // 進行・自動停止を保つため常時回す。venueTab は軽いので常時
    const auto uiShowing = isShowing();
    const auto liveMode = liveOverlay.isVisible();
    const auto currentTab = tabs.getCurrentTabIndex();

    if (uiShowing && ! liveMode && currentTab == 0) transferTab.timerTick();
    if (uiShowing && ! liveMode && currentTab == 1) delayTab.timerTick();
    else delayTab.timerTickBackground(); // ループバック測定の進行は非表示でも止めない（issue #55）
    if (uiShowing && ! liveMode && currentTab == 2) rtaSplTab.timerTick();
    else rtaSplTab.timerTickBackground(); // SPL校正待ちの進行も同様（issue #55）
    if (uiShowing && ! liveMode && currentTab == 3) eqTab.timerTick();
    limitTab.timerTick();
    venueTab.timerTick();

    if (uiShowing && ! liveMode && currentTab == 5)
        utilitiesTab.timerTick();

    if (liveMode)
        liveOverlay.timerTick();

    measurementPanel.setAverageCountDisplay (engine.getMeasurement().getAverages());

    const auto status = engine.getStatusText();
    const auto watchdogWarningActive = (std::int32_t) (uiWatchdogWarningUntilMs - nowMs) > 0;
    const auto deviceStopNoticeActive = (std::int32_t) (deviceStopNoticeUntilMs - nowMs) > 0;
    bottomBar.setStatusText (watchdogWarningActive ? strings::uiWatchdogStoppedNotice
                             : deviceStopNoticeActive ? strings::deviceRestartStoppedNotice
                             : status.isNotEmpty() ? status : eeq::strings::noDeviceInfo);

    // デバイスが開き直されたら測定設定（サンプルレート）を合わせ直す
    if (engine.getDeviceGeneration() != lastSeenGeneration)
    {
        lastSeenGeneration = engine.getDeviceGeneration();
        applyMeasurementSettings();

        // RTA/SPL解析器は RtaSplTab が保存済みのユーザー設定で張り直す
        //（ここでハードコードするとユーザーの設定が上書きされるため触らない）
    }

    if (engine.consumeStoppedByDeviceRestart())
    {
        // デバイスが止まったので励起を止めた。再オープン後も自動では鳴らさない（issue #102）
        deviceStopNoticeUntilMs = nowMs + 8000u;
        eeq::log::write ("excitation: stopped because the audio device stopped (not resumed automatically)");
    }

    if (engine.consumeNoiseReachedIdle())
        eeq::log::write ("excitation: reached idle (fade-out completed on audio thread)");

    if (engine.consumeExcitationFinished())
        eeq::log::write ("excitation: playback reached the end");

    juce::String fallbackNotice;

    if (engine.consumeFallbackNotice (fallbackNotice))
    {
        eeq::log::write ("audio: " + fallbackNotice);
        showError (fallbackNotice);
    }

    juce::String deviceError;

    if (engine.consumeDeviceError (deviceError))
    {
        // デバイスエラーの記録と測定停止はここ（メッセージスレッド）で行う（issue #57）
        eeq::log::write ("audio device error: " + deviceError + " (noise and measurement stopped)");
        engine.getMeasurement().stopMeasurement();
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

    // Space = 測定の開始/停止だけ（発振器は鳴らさない: issue #131）。
    // 止めるときも発振器は止めない（音楽ソースや校正音で測っている場合があるため）
    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        auto& measurement = engine.getMeasurement();

        if (measurement.isMeasuring())
            measurement.stopMeasurement();
        else
            measurement.startMeasurement();

        return true;
    }

    // N = 発振器（励起信号）の開始/停止（issue #141）
    if (key == 'n' || key == 'N')
    {
        if (engine.isNoiseAudible())
            engine.stopExcitation();
        else
            engine.startExcitation();

        return true;
    }

    // D = 遅延を検出して適用（Delay Finder: issue #142）
    if (key == 'd' || key == 'D')
    {
        tabs.setCurrentTabIndex (1);
        delayTab.runDelayMeasurement (true);
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

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused (originatingComponent);

    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
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
