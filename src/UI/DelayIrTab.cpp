#include "DelayIrTab.h"

#include "../Core/Acoustics.h"
#include "../Core/Exporters.h"
#include "../Core/Log.h"
#include "../Core/Utilities.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

namespace
{
constexpr double defaultTemperatureC = 20.0;

/** ループバック測定の妥当性検証（issue #31）。不合格の理由を返す（空文字なら合格）。
    ケーブル直結の電気系統は「無音でなく・高コヒーレンス・平坦」であることを使って、
    部屋の測定結果を誤って補正として保存することを防ぐ */
juce::String validateLoopbackFrame (const eeq::dsp::SpectrumFrame& frame)
{
    if (frame.fftSize < 256 || frame.averages < 16)
        return juce::String (u8"平均が不足しています（16回以上の平均が必要です）");

    // 無音でないこと: Meas 入力の実効パワーが -60 dBFS を超える bin がある
    float bestMeas = -300.0f;

    for (const auto power : frame.measPowerDb)
        bestMeas = std::max (bestMeas, power);

    if (bestMeas < -60.0f)
        return juce::String (u8"入力に信号がありません（出力→入力の接続と、入力chの割り当てを確認してください）");

    // 100 Hz〜10 kHz の bin について、コヒーレンス ≥ 0.95 が 80% 以上、
    // 振幅が ±3 dB 以内（=範囲6 dB以内）であること
    const auto binWidth = frame.sampleRate / (double) frame.fftSize;
    const auto kLo = juce::jlimit (1, (int) frame.transfer.size() - 1, (int) std::ceil (100.0 / binWidth));
    const auto kHi = juce::jlimit (1, (int) frame.transfer.size() - 1, (int) std::floor (10000.0 / binWidth));

    if (kHi <= kLo)
        return juce::String (u8"FFTサイズに対して検証帯域が取れません");

    int coherentCount = 0, totalCount = 0;
    auto minDb = 1.0e30f;
    auto maxDb = -1.0e30f;

    for (int k = kLo; k <= kHi; ++k)
    {
        ++totalCount;

        if (frame.coherence[(size_t) k] >= 0.95f)
            ++coherentCount;

        minDb = std::min (minDb, frame.magnitudeDb[(size_t) k]);
        maxDb = std::max (maxDb, frame.magnitudeDb[(size_t) k]);
    }

    if (totalCount == 0 || (double) coherentCount / (double) totalCount < 0.8)
        return juce::String (u8"ループバックになっていません（マイク入力に部屋の音が入っています。通常測定を止めてから押してください）");

    if (maxDb - minDb > 6.0f)
        return juce::String (u8"周波数特性が平坦ではありません（±3 dB以内が必要です。現在の差: ")
             + juce::String (maxDb - minDb, 1) + juce::String (u8" dB）");

    return {};
}
} // namespace

// 系統リストのモデル
class DelayIrTab::SystemListModel : public juce::ListBoxModel
{
public:
    explicit SystemListModel (DelayIrTab& ownerIn) : owner (ownerIn) {}

    int getNumRows() override
    {
        return (int) owner.ctx.project.currentVenue().systems.size();
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        const auto& systems = owner.ctx.project.currentVenue().systems;

        if (rowNumber < 0 || rowNumber >= (int) systems.size())
            return;

        const auto& system = systems[(size_t) rowNumber];

        g.fillAll (rowIsSelected ? theme::get (theme::ColourId::control)
                                 : theme::get (theme::ColourId::panelBackground));

        g.setColour (system.colour);
        g.fillRoundedRectangle (6.0f, (float) height / 2 - 6.0f, 12.0f, 12.0f, 2.0f);

        g.setColour (theme::get (theme::ColourId::text));
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText (system.name + "  (ch" + juce::String (system.outputChannel + 1) + ")",
                    24, 0, width - 28, height, juce::Justification::centredLeft);

        if (rowNumber == owner.exciteTarget)
        {
            g.setColour (theme::get (theme::ColourId::accent));
            g.drawText (u8"◀", width - 24, 0, 20, height, juce::Justification::centred);
        }
    }

    void listBoxItemDoubleClicked (int rowNumber, const juce::MouseEvent&) override
    {
        owner.setExciteTarget (rowNumber);
    }

    void selectedRowsChanged (int) override
    {
        // 選択が変わったら名前/出力chの編集欄へ反映する（issue #15）
        owner.refreshSystemEditors();
    }

private:
    DelayIrTab& owner;
};

DelayIrTab::DelayIrTab (UiContext& context)
    : ctx (context)
{
    // ---- 遅延パネル ----
    delayTitle.setText (strings::delayLabel, juce::dontSendNotification);
    styleLabel (delayTitle, 13.0f, false);
    addAndMakeVisible (delayTitle);

    styleLabelWithColour (delayValueLabel, theme::ColourId::text, 20.0f, true);
    addAndMakeVisible (delayValueLabel);

    styleAccentButton (measureButton);
    measureButton.setButtonText (strings::delayMeasureButton);
    measureButton.onClick = [this] { runDelayMeasurement(); };
    addAndMakeVisible (measureButton);

    styleButton (applyButton);
    applyButton.setButtonText (strings::delayApplyButton);
    applyButton.onClick = [this] { applyDelay(); };
    addAndMakeVisible (applyButton);

    styleButton (clearButton);
    clearButton.setButtonText (strings::delayClearButton);
    clearButton.onClick = [this] { clearDelay(); };
    addAndMakeVisible (clearButton);

    // GCC-PHAT を既定で有効にする（issue #34）: 振幅相関だと低域のエネルギーが支配し、
    // ピンクノイズでの到達時間がサブ〜ウーファー帯で決まってしまう。
    // PHAT はスペクトルを白色化するため、帯域依存の偏りがなくなる
    phatToggle.setToggleState (true, juce::dontSendNotification);
    phatToggle.setButtonText (strings::delayPhatToggle);
    styleToggle (phatToggle);
    addAndMakeVisible (phatToggle);

    rawPhaseToggle.setButtonText (strings::delayRawPhaseToggle);
    rawPhaseToggle.onClick = [this]
    {
        ctx.state.showRawPhase.store (rawPhaseToggle.getToggleState());
    };
    styleToggle (rawPhaseToggle);
    addAndMakeVisible (rawPhaseToggle);

    delayProposalLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (delayProposalLabel);

    distanceCheckLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (distanceCheckLabel);

    manualWarnLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::warn));
    addAndMakeVisible (manualWarnLabel);

    // 手動入力（ms と m が連動。仕様 3.3.2）
    manualDelayTitle.setText (strings::delayLabel + " (ms)", juce::dontSendNotification);
    manualDistanceTitle.setText (strings::delayDistanceLabel + " (m)", juce::dontSendNotification);
    styleLabel (manualDelayTitle);
    styleLabel (manualDistanceTitle);
    addAndMakeVisible (manualDelayTitle);
    addAndMakeVisible (manualDistanceTitle);

    styleTextEditor (manualDelayEditor);
    styleTextEditor (manualDistanceEditor);
    manualDelayEditor.setInputRestrictions (10, "0123456789.-");
    manualDistanceEditor.setInputRestrictions (10, "0123456789.-");
    manualDelayEditor.onTextChange = [this] { refreshDistanceFromManualDelay(); };
    styleButton (manualApplyButton);
    manualApplyButton.setButtonText (strings::delayApplyManualButton);
    manualApplyButton.onClick = [this]
    {
        const auto text = manualDelayEditor.getText().trim();
        const auto ms = text.getDoubleValue();
        const auto sampleRate = ctx.engine.getCurrentSampleRate();

        if (text.isEmpty() || ! std::isfinite (ms) || ms < 0.0
            || ! std::isfinite (sampleRate) || sampleRate <= 0.0)
        {
            const auto invalidDelay = text.isEmpty() || ! std::isfinite (ms) || ms < 0.0;
            manualWarnLabel.setText (invalidDelay
                                         ? juce::String (u8"遅延 (ms) は 0 以上の数値を入力してください")
                                         : juce::String (u8"有効なサンプルレートがありません"),
                                     juce::dontSendNotification);
            return;
        }

        ctx.state.manualDelayMs.store (ms);
        ctx.state.appliedDelaySamples.store (eeq::acoustics::samplesForDelayMs (ms, sampleRate));
        tempAtDelayApply = ctx.state.airTemperatureC.load();
        manualWarnLabel.setText ({}, juce::dontSendNotification);
        eeq::log::write (juce::String ("delay: manually applied ") + juce::String (ms, 2) + " ms");
    };
    addAndMakeVisible (manualApplyButton);
    manualDistanceEditor.onTextChange = [this]
    {
        // 距離→ms（気温で換算。仕様 3.3.2）
        const auto metres = manualDistanceEditor.getText().getDoubleValue();

        if (metres > 0.0)
        {
            const auto ms = eeq::acoustics::delayMillisecondsForDistance (metres, ctx.state.airTemperatureC.load());
            manualDelayEditor.setText (juce::String (ms, 2), juce::dontSendNotification);
        }
    };
    addAndMakeVisible (manualDelayEditor);
    addAndMakeVisible (manualDistanceEditor);

    // ---- IR ----
    irTitle.setText (strings::irLabel, juce::dontSendNotification);
    styleLabel (irTitle, 13.0f, false);
    addAndMakeVisible (irTitle);

    styleButton (irExportButton);
    irExportButton.setButtonText (strings::exportIrWav);
    irExportButton.onClick = [this]
    {
        // 最新の測定から IR を作って書き出す（仕様 6.2: 32bit float WAV）
        eeq::dsp::SpectrumFrame frame;
        ctx.engine.getMeasurement().copyLatestResult (frame);

        if (frame.averages == 0)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                strings::snapshotExportTitle,
                juce::String (u8"測定結果がまだありません。測定を開始してから IR を書き出してください。"),
                strings::okButton, this, nullptr);
            return;
        }

        auto chooser = std::make_shared<juce::FileChooser> ("impulse_response.wav",
                                                            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                            "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [chooser, frame] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();

                if (file == juce::File())
                    return;

                const auto error = eeq::core::exporters::writeImpulseResponseWav (
                    eeq::dsp::buildImpulseResponse (frame), file);

                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
            });
    };
    addAndMakeVisible (irExportButton);

    peakAlignToggle.setButtonText (strings::irPeakAlignToggle);
    peakAlignToggle.setToggleState (true, juce::dontSendNotification);
    styleToggle (peakAlignToggle);
    peakAlignToggle.onClick = [this]
    {
        peakAligned = peakAlignToggle.getToggleState();
        repaint();
    };
    addAndMakeVisible (peakAlignToggle);

    etcToggle.setButtonText (strings::irEtcToggle);
    etcToggle.setToggleState (true, juce::dontSendNotification);
    styleToggle (etcToggle);
    etcToggle.onClick = [this]
    {
        showEtc = etcToggle.getToggleState();
        repaint();
    };
    addAndMakeVisible (etcToggle);

    // ---- ループバック校正（仕様 6.1）----
    loopbackTitle.setText (strings::loopbackSection, juce::dontSendNotification);
    styleLabel (loopbackTitle, 13.0f, false);
    addAndMakeVisible (loopbackTitle);

    styleAccentButton (loopbackMeasureButton);
    loopbackMeasureButton.setButtonText (strings::loopbackMeasureButton);
    loopbackMeasureButton.onClick = [this] { measureLoopback(); };
    addAndMakeVisible (loopbackMeasureButton);

    styleButton (loopbackClearButton);
    loopbackClearButton.setButtonText (strings::loopbackClearButton);
    loopbackClearButton.onClick = [this]
    {
        ctx.correction.clear();
        ctx.settings.setValue ("loopbackCorrection", juce::var());
        ctx.settings.saveIfNeeded();
        updateLoopbackUi();
    };
    addAndMakeVisible (loopbackClearButton);

    loopbackEnableToggle.setButtonText (strings::loopbackEnableToggle);
    loopbackEnableToggle.setToggleState (true, juce::dontSendNotification);
    loopbackEnableToggle.onClick = [this]
    {
        ctx.state.loopbackEnabled.store (loopbackEnableToggle.getToggleState());
    };
    styleToggle (loopbackEnableToggle);
    addAndMakeVisible (loopbackEnableToggle);

    loopbackStatusLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (loopbackStatusLabel);

    // ---- 系統管理（仕様 3.3.4）----
    systemsTitle.setText (strings::systemsSection, juce::dontSendNotification);
    styleLabel (systemsTitle, 13.0f, false);
    addAndMakeVisible (systemsTitle);

    systemsHint.setText (strings::systemsMuteOthersHint, juce::dontSendNotification);
    styleLabel (systemsHint, 11.0f);
    addAndMakeVisible (systemsHint);

    styleButton (systemsAddButton);
    systemsAddButton.setButtonText (strings::systemsAddButton);
    systemsAddButton.onClick = [this]
    {
        auto& systems = ctx.project.currentVenue().systems;
        const int paletteCount = 6;
        eeq::core::SpeakerSystemInfo system;
        system.name = juce::String (u8"系統") + juce::String (systems.size() + 1);

        // 未使用の最小出力chを割り当てる（issue #15: すべて ch0 固定だと個別測定が成立しない）
        uint64_t used = 0;

        for (const auto& existing : systems)
            used |= (uint64_t) 1 << juce::jlimit (0, 63, existing.outputChannel);

        int freeChannel = 0;

        while (freeChannel < 64 && (used & ((uint64_t) 1 << freeChannel)) != 0)
            ++freeChannel;

        system.outputChannel = freeChannel;
        system.colour = theme::get ((theme::ColourId) ((int) theme::ColourId::snapshotPalette1
                                                     + systems.size() % paletteCount));
        systems.push_back (system);
        saveSystemsToVenue();
        rebuildSystemList();
        systemList.selectRow ((int) systems.size() - 1);
        refreshSystemEditors();
    };
    addAndMakeVisible (systemsAddButton);

    styleButton (systemsRemoveButton);
    systemsRemoveButton.setButtonText (strings::systemsRemoveButton);
    systemsRemoveButton.onClick = [this]
    {
        auto& systems = ctx.project.currentVenue().systems;
        const auto selected = systemList.getSelectedRow();

        if (selected >= 0 && selected < (int) systems.size())
        {
            systems.erase (systems.begin() + selected);
            saveSystemsToVenue();
            rebuildSystemList();
        }
    };
    addAndMakeVisible (systemsRemoveButton);

    styleButton (systemsAllButton);
    systemsAllButton.setButtonText (strings::systemsAll);
    systemsAllButton.onClick = [this] { setExciteTarget (-1); };
    addAndMakeVisible (systemsAllButton);

    systemListModel = std::make_unique<SystemListModel> (*this);
    systemList.setModel (systemListModel.get());
    systemList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    systemList.setRowHeight (22);
    addAndMakeVisible (systemList);

    // ---- 系統の編集（名前・出力ch。issue #15）----
    styleTextEditor (systemNameEditor);
    systemNameEditor.setTextToShowWhenEmpty (juce::String (u8"系統名"),
                                             theme::get (theme::ColourId::textDim));
    systemNameEditor.onTextChange = [this]
    {
        const auto row = systemList.getSelectedRow();
        auto& systems = ctx.project.currentVenue().systems;

        if (row >= 0 && row < (int) systems.size())
        {
            const auto newName = systemNameEditor.getText().trim();

            if (newName.isNotEmpty() && newName != systems[(size_t) row].name)
            {
                systems[(size_t) row].name = newName;
                saveSystemsToVenue();
                systemList.updateContent();
            }
        }
    };
    addAndMakeVisible (systemNameEditor);

    styleCombo (systemChannelCombo);
    systemChannelCombo.onChange = [this]
    {
        const auto row = systemList.getSelectedRow();
        auto& systems = ctx.project.currentVenue().systems;
        const auto channel = systemChannelCombo.getSelectedId() - 1;

        if (row >= 0 && row < (int) systems.size() && channel >= 0
            && channel != systems[(size_t) row].outputChannel)
        {
            systems[(size_t) row].outputChannel = channel;
            saveSystemsToVenue();
            systemList.updateContent();

            // 測定対象がこの系統だった場合はルーティングも張り直す
            if (exciteTarget == row)
                ctx.engine.setOutputRoutingMask ((uint64_t) 1 << channel);
        }
    };
    addAndMakeVisible (systemChannelCombo);
    refreshChannelComboChoices();

    // ---- 系統比較（3.3.1 / 3.3.3 / 3.3.5）----
    compareTitle.setText (strings::compareSection, juce::dontSendNotification);
    styleLabel (compareTitle, 13.0f, false);
    addAndMakeVisible (compareTitle);

    compareARowLabel.setText (strings::compareSelectA, juce::dontSendNotification);
    compareBRowLabel.setText (strings::compareSelectB, juce::dontSendNotification);
    styleLabel (compareARowLabel);
    styleLabel (compareBRowLabel);
    addAndMakeVisible (compareARowLabel);
    addAndMakeVisible (compareBRowLabel);

    for (auto* combo : { &compareComboA, &compareComboB })
    {
        styleCombo (*combo);
        combo->onChange = [this] { updateCompare(); };
        addAndMakeVisible (*combo);
    }

    compareResultLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (compareResultLabel);

    styleLabelWithColour (compareTowerLabel, theme::ColourId::accent, 20.0f, true);
    addAndMakeVisible (compareTowerLabel);

    comparePolarityLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (comparePolarityLabel);

    compareCrossoverLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (compareCrossoverLabel);

    compareFineLabel.setText (strings::compareFineAdjust, juce::dontSendNotification);
    styleLabel (compareFineLabel);
    addAndMakeVisible (compareFineLabel);

    styleSlider (compareFineSlider);
    compareFineSlider.setRange (-20.0, 20.0, 0.1);
    compareFineSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    compareFineSlider.onValueChange = [this] { updateCompare(); };
    addAndMakeVisible (compareFineSlider);

    // 気温（仕様 3.3.2: 既定 20 ℃。UIからの変更はすべての換算に反映する）
    updateDelayReadout();
    updateLoopbackUi();
    rebuildSystemList();
}

DelayIrTab::~DelayIrTab() = default; // SystemListModel の完全型が必要（unique_ptr）

void DelayIrTab::timerTick()
{
    // デバイスが開き直されたら出力chの選択肢を作り直す（issue #15）
    if (ctx.engine.getDeviceGeneration() != seenDeviceGenerationForChannels)
        refreshChannelComboChoices();

    // ループバック校正の自動進行（issue #17/#31/#55）
    processLoopbackRanging();

    // IRの再構築が必要か（平均数・幅・FFTサイズが変わったら）
    auto framePtr = ctx.engine.getMeasurement().getLatestFrame();
    if (framePtr)
        rebuildIrIfDirty (*framePtr);

    // ---- 温度変化による遅延のずれ予測（仕様 3.8）----
    // 遅延を適用したときの気温を基準に、現在の気温でのずれを計算する
    if (ctx.state.appliedDelaySamples.load() != 0.0 && ctx.engine.getCurrentSampleRate() > 0.0)
    {
        const auto appliedMs = eeq::acoustics::delayMsForSamples (ctx.state.appliedDelaySamples.load(),
                                                                  ctx.engine.getCurrentSampleRate());
        const auto tempNow = ctx.state.airTemperatureC.load();

        if (tempAtDelayApply < 900.0)
        {
            // 同じ距離を新しい気温で伝わる時間を計算し、ずれを出す
            const auto distance = eeq::acoustics::distanceMetresForDelay (appliedMs, tempAtDelayApply);
            const auto delayNow = eeq::acoustics::delayMillisecondsForDistance (distance, tempNow);
            const auto drift = delayNow - appliedMs;

            if (std::abs (drift) > 0.5) // しきい値は既定 0.5 ms（仕様 3.8）
            {
                delayProposalLabel.setText (juce::String (strings::temperatureDriftWarning)
                                                + juce::String (drift, 2) + strings::temperatureDriftSuffix,
                                            juce::dontSendNotification);
                delayProposalLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::warn));
            }
            else if (delayProposalLabel.getText().contains (strings::temperatureDriftWarning))
            {
                delayProposalLabel.setText ({}, juce::dontSendNotification);
            }
        }
    }

    // スナップショット一覧の変化を比較コンボへ反映（選択中のA/Bはできるだけ保持する）
    const auto version = (int) ctx.state.snapshotVersion.load();

    if (version != compareVersionSeen)
    {
        compareVersionSeen = version;
        auto& snapshots = ctx.project.snapshots();

        const auto previousA = compareComboA.getSelectedId();
        const auto previousB = compareComboB.getSelectedId();

        auto fill = [&snapshots] (juce::ComboBox& combo)
        {
            combo.clear (juce::dontSendNotification);

            for (int i = 0; i < (int) snapshots.size(); ++i)
                combo.addItem (snapshots[(size_t) i].name, i + 1);
        };

        fill (compareComboA);
        fill (compareComboB);

        // 前の選択がまだ有効なら維持、無効なら既定（A=最初, B=2番目）へ
        if (previousA < 1 || previousA > (int) snapshots.size())
            compareComboA.setSelectedId (snapshots.empty() ? 0 : 1, juce::dontSendNotification);
        else
            compareComboA.setSelectedId (previousA, juce::dontSendNotification);

        if (previousB < 1 || previousB > (int) snapshots.size())
            compareComboB.setSelectedId (snapshots.size() >= 2 ? 2 : (snapshots.empty() ? 0 : 1),
                                         juce::dontSendNotification);
        else
            compareComboB.setSelectedId (previousB, juce::dontSendNotification);

        updateCompare();
    }

    // ループバック校正の鮮度（仕様 6.1: 接続・レート・ゲインが変わったら促す）
    if (ctx.correction.isActive())
    {
        auto framePtr = ctx.engine.getMeasurement().getLatestFrame();
        if (framePtr)
        {
            const auto stale = ! ctx.correction.matches (*framePtr);
            loopbackStatusLabel.setColour (juce::Label::textColourId,
                                           stale ? theme::get (theme::ColourId::warn)
                                                 : theme::get (theme::ColourId::textDim));
        }
    }
}

void DelayIrTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    // ---- IRグラフ（時間軸。仕様 3.3）----
    // 描画領域は resized() の左カラム配置と一致させる（全幅で描くと右カラムの
    // パネル背面までカーブが伸びる）。タイトル行・書き出しボタン行・トグル行は
    // 子コンポーネントの領域なので、グラフ本体からは除く
    auto paintBounds = getLocalBounds().reduced (8);
    auto leftColumn = paintBounds.removeFromLeft (paintBounds.getWidth() * 3 / 5);
    auto irAreaInt = leftColumn.removeFromTop (irGraphHeight);
    irAreaInt.removeFromTop (18 + 26);
    irAreaInt.removeFromBottom (20);
    const auto irArea = irAreaInt.toFloat();

    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (irArea, 6.0f);

    if (cachedIr == nullptr || cachedIr->samples.empty())
    {
        g.setColour (theme::get (theme::ColourId::textDim));
        g.drawText (strings::graphPlaceholder, irArea.toNearestInt(), juce::Justification::centred);
        return;
    }

    // 時間軸: ピークを基準に ±irTimeRange。ETC（対数エンベロープ）か波形かを切替
    const auto& ir = *cachedIr;
    const auto sampleRate = ir.sampleRate;
    const auto peakMs = ir.peakTimeSamples * 1000.0 / sampleRate;
    const auto t0 = peakAligned ? peakMs + irTimeRangeMs.getStart() : irTimeRangeMs.getStart();
    const auto t1 = peakAligned ? peakMs + irTimeRangeMs.getEnd() : irTimeRangeMs.getEnd();

    auto timeToX = [&] (double timeMs)
    {
        const auto t = juce::jlimit (0.0, 1.0, (timeMs - t0) / std::max (1.0e-9, t1 - t0));
        return irArea.getX() + (float) t * irArea.getWidth();
    };

    // 0msグリッド
    g.setColour (theme::get (theme::ColourId::gridMajor));
    g.drawLine (timeToX (0.0), irArea.getY(), timeToX (0.0), irArea.getBottom(), 1.0f);

    // 時間ラベル
    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (10.5f)));

    for (double ms = std::ceil (t0 / 10.0) * 10.0; ms < t1; ms += 10.0)
    {
        const auto x = timeToX (ms);
        g.drawLine (x, irArea.getBottom(), x, irArea.getBottom() - 4.0f, 1.0f);
        g.drawText (juce::String (ms, 0), (int) x - 18, (int) irArea.getBottom() + 2, 36, 12,
                    juce::Justification::centred);
    }

    juce::Path curve;

    if (showEtc)
    {
        // ETC（対数エンベロープ。仕様 3.3）
        float maxDb = -300.0f, minDb = 0.0f;
        std::vector<float> etc (ir.samples.size());

        for (size_t i = 0; i < ir.samples.size(); ++i)
        {
            etc[i] = 20.0f * std::log10 (juce::jmax (std::abs (ir.samples[i]), 1.0e-6f));
            maxDb = std::max (maxDb, etc[i]);
            minDb = std::min (minDb, etc[i]);
        }

        minDb = std::max (minDb, maxDb - 80.0f);

        auto etcY = [&] (float db)
        { return irArea.getBottom() - (db - minDb) / std::max (1.0f, maxDb - minDb) * (irArea.getHeight() - 24.0f); };

        for (size_t i = 0; i < ir.samples.size(); ++i)
        {
            const auto timeMs = (double) i * 1000.0 / sampleRate;

            if (timeMs < t0 || timeMs > t1)
                continue;

            const auto x = timeToX (timeMs);
            const auto y = etcY (etc[i]);

            if (curve.isEmpty())
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }

        g.setColour (theme::get (theme::ColourId::accent));
        g.strokePath (curve, juce::PathStrokeType (1.2f));
    }
    else
    {
        // 波形
        float peak = 1.0e-6f;

        for (const auto sample : ir.samples)
            peak = std::max (peak, std::abs (sample));

        const auto midY = (irArea.getY() + irArea.getBottom() - 24.0f) / 2.0f;
        const auto scaleY = (irArea.getHeight() - 24.0f) / 2.0f / peak;

        for (size_t i = 0; i < ir.samples.size(); ++i)
        {
            const auto timeMs = (double) i * 1000.0 / sampleRate;

            if (timeMs < t0 || timeMs > t1)
                continue;

            const auto x = timeToX (timeMs);
            const auto y = midY - ir.samples[i] * scaleY;

            if (curve.isEmpty())
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);
        }

        g.setColour (theme::get (theme::ColourId::accent));
        g.strokePath (curve, juce::PathStrokeType (1.0f));

        // 中線
        g.setColour (theme::get (theme::ColourId::grid));
        g.drawLine (irArea.getX(), midY, irArea.getRight(), midY, 1.0f);
    }
}

void DelayIrTab::rebuildIrIfDirty (const eeq::dsp::SpectrumFrame& frame)
{
    const auto width = juce::jmax (100, getWidth() - 100);

    // 平均回数の増加による再構築は250 ms以上の間隔を空ける（issue #37）:
    // 測定中は15 Hzで平均が増えるが、IRの再構築（2N点の逆FFT）をその頻度で
    // 行う必要はなく、見た目も4回/秒で十分追従する。サイズやレートの変更は
    // 即座に反映させるため間引かない
    const auto now = juce::Time::getCurrentTime();

    if (frame.averages > cachedIrAverages && now < irRebuildAllowed)
        return;

    // サンプルレートも含めてキャッシュの鍵にする（レートが変わると時間軸がずれる）
    if (frame.averages > 0 && (frame.averages != cachedIrAverages || width != cachedIrWidth
                               || frame.fftSize != cachedIrFftSize
                               || std::abs (frame.sampleRate - cachedIrSampleRate) > 0.5))
    {
        cachedIr = std::make_unique<eeq::dsp::ImpulseResponse> (eeq::dsp::buildImpulseResponse (frame));
        cachedIrAverages = frame.averages;
        cachedIrWidth = width;
        cachedIrFftSize = frame.fftSize;
        cachedIrSampleRate = frame.sampleRate;
        irRebuildAllowed = now + juce::RelativeTime::milliseconds (250);
        repaint();
    }
}

void DelayIrTab::runDelayMeasurement()
{
    usePhat = phatToggle.getToggleState();
    lastEstimate = ctx.engine.getMeasurement().estimateCurrentDelay (usePhat);
    hasEstimate = true;
    updateDelayReadout();
}

void DelayIrTab::updateDelayReadout()
{
    const auto sampleRate = ctx.engine.getCurrentSampleRate();
    const auto temperature = ctx.state.airTemperatureC.load();

    if (! hasEstimate || ! lastEstimate.valid)
    {
        delayValueLabel.setText (strings::delayNotDetected, juce::dontSendNotification);
        applyButton.setEnabled (false);
        return;
    }

    applyButton.setEnabled (true);

    // 表示: ms / サンプル / 距離（気温から。仕様 3.3）
    const auto ms = eeq::acoustics::delayMsForSamples (lastEstimate.delaySamples, sampleRate);
    const auto metres = eeq::acoustics::distanceMetresForDelay (ms, temperature);
    const auto feet = metres * 3.28084;

    delayValueLabel.setText (juce::String (ms, 2) + " ms  /  "
                                 + juce::String (lastEstimate.delaySamples, 1) + " " + strings::delaySamplesUnit
                                 + "  /  " + juce::String (metres, 2) + " m  /  " + juce::String (feet, 1) + " ft",
                             juce::dontSendNotification);

    // 提案値の妥当性チェック（仕様 3.3.2）: 距離を算出して確認を促す
    const auto checkText = strings::delayDistanceCheckLabel + " " + juce::String (metres, 1) + " "
                         + strings::delayDistanceCheckSuffix;
    distanceCheckLabel.setText (checkText, juce::dontSendNotification);

    // 手動入力と10%以上ずれていたら警告（仕様 3.3.2）
    const auto manualMetres = manualDistanceEditor.getText().getDoubleValue();

    if (manualMetres > 0.0 && std::abs (manualMetres - metres) > manualMetres * 0.10)
        manualWarnLabel.setText (strings::delayDistanceMismatch, juce::dontSendNotification);
    else
        manualWarnLabel.setText ({}, juce::dontSendNotification);
}

void DelayIrTab::applyDelay()
{
    if (! hasEstimate || ! lastEstimate.valid)
        return;

    ctx.state.appliedDelaySamples.store (lastEstimate.delaySamples);
    tempAtDelayApply = ctx.state.airTemperatureC.load(); // 温度ずれ予測の基準（仕様 3.8）
    eeq::log::write (juce::String ("delay: applied ") + juce::String (lastEstimate.delaySamples, 2) + " samples");

    // 手動入力欄に値を反映する（ms）
    const auto ms = eeq::acoustics::delayMsForSamples (lastEstimate.delaySamples,
                                                       ctx.engine.getCurrentSampleRate());
    manualDelayEditor.setText (juce::String (ms, 2), juce::dontSendNotification);

    // 内部参照のときは、システム補正で測った往復遅延を差し引いた
    // 「音が空間を伝わった時間」も表示する（仕様 3.3.2）
    if (ctx.correction.isActive())
    {
        const auto roundtripMs = eeq::acoustics::delayMsForSamples (ctx.correction.getRoundtripDelaySamples(),
                                                                    ctx.engine.getCurrentSampleRate());
        delayProposalLabel.setText (juce::String (strings::systemDelayLabel) + ": "
                                        + juce::String (roundtripMs, 2) + " ms / "
                                        + strings::spatialDelayLabel + ": "
                                        + juce::String (std::max (0.0, ms - roundtripMs), 2) + " ms",
                                    juce::dontSendNotification);
        delayProposalLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    }
}

void DelayIrTab::clearDelay()
{
    ctx.state.appliedDelaySamples.store (0.0);
}

void DelayIrTab::refreshDistanceFromManualDelay()
{
    const auto ms = manualDelayEditor.getText().getDoubleValue();
    ctx.state.manualDelayMs.store (ms);

    if (ms > 0.0)
    {
        const auto metres = eeq::acoustics::distanceMetresForDelay (ms, ctx.state.airTemperatureC.load());
        manualDistanceEditor.setText (juce::String (metres, 2), juce::dontSendNotification);
    }

    updateDelayReadout();
}

void DelayIrTab::measureLoopback()
{
    // 出力→入力のループバック測定（仕様 6.1）。1回のクリックで
    // 「励起開始 → 測定開始 → 平均が溜まるまで待つ → 検証して保存 → 停止」を自動で行う
    //（issue #17: 押しても何も起きないをなくす。issue #31: 誤った保存を防ぐ）
    if (loopbackRanging)
        return; // 進行中は無視（ボタンは「測定中…」になり無効化されている）

    auto& measurement = ctx.engine.getMeasurement();

    // ループバック専用の測定としてやり直す。通常測定の平均を使わないので、
    // 測定中の部屋の応答を誤って保存することがない
    measurement.resetAverages();
    measurement.startMeasurement();
    ctx.engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
    ctx.engine.startExcitation();

    loopbackRanging = true;
    loopbackDeadline = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (12.0);
    loopbackMeasureButton.setEnabled (false);
    loopbackMeasureButton.setButtonText (strings::loopbackMeasuring);
    loopbackStatusLabel.setText (juce::String (u8"ループバックを測定中です（出力→入力をケーブルで接続してください）"),
                                 juce::dontSendNotification);
    eeq::log::write ("loopback: measurement started (waiting for averages)");
}

void DelayIrTab::finishLoopbackMeasurement()
{
    loopbackRanging = false;
    loopbackMeasureButton.setEnabled (true);
    loopbackMeasureButton.setButtonText (strings::loopbackMeasureButton);

    auto& measurement = ctx.engine.getMeasurement();
    measurement.stopMeasurement();
    ctx.engine.stopExcitation();

    eeq::dsp::SpectrumFrame frame;
    measurement.copyLatestResult (frame);

    const auto error = validateLoopbackFrame (frame);

    if (error.isNotEmpty())
    {
        // 検証に落ちたものは保存しない（issue #31: 誤った補正で以降の全測定を汚染しない）
        loopbackStatusLabel.setText (error, juce::dontSendNotification);
        loopbackStatusLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::danger));
        eeq::log::write ("loopback: validation FAILED: " + error);
        return;
    }

    const auto estimate = measurement.estimateCurrentDelay (false);
    ctx.correction.setCorrection (frame, estimate.valid ? estimate.delaySamples : 0.0);

    // 補正をアプリ設定へ保存する（issue #21: 再起動で消えない）。
    // どの配線・レートで測ったかも一緒に残す。fftSize/レートが変わった場合は
    // matches() が自動で適用を止める（要再測定）
    ctx.settings.setValue ("loopbackCorrection", juce::JSON::toString (ctx.correction.toVar()));
    ctx.settings.setValue ("loopbackDevice", ctx.engine.getSelectedInputDeviceName()
                                                + " / " + ctx.engine.getSelectedOutputDeviceName());
    ctx.settings.setValue ("loopbackRate", frame.sampleRate);
    ctx.settings.saveIfNeeded();

    eeq::log::write (juce::String ("loopback correction measured: delay=")
                     + juce::String (ctx.correction.getRoundtripDelaySamples(), 2) + " samples, averages="
                     + juce::String (frame.averages));
    updateLoopbackUi();
}

void DelayIrTab::updateLoopbackUi()
{
    if (! ctx.correction.isActive())
    {
        loopbackStatusLabel.setText (strings::loopbackInstruction, juce::dontSendNotification);
        loopbackEnableToggle.setEnabled (false);
        return;
    }

    loopbackEnableToggle.setEnabled (true);
    loopbackStatusLabel.setText (juce::String (u8"測定済み（往復遅延 ")
                                     + juce::String (ctx.correction.getRoundtripDelaySamples(), 1)
                                     + " " + strings::delaySamplesUnit + juce::String (u8"） ") + strings::loopbackStaleWarning,
                                 juce::dontSendNotification);
}

void DelayIrTab::processLoopbackRanging()
{
    // ループバック校正の自動進行（issue #17/#31/#55）: 平均が溜まったら検証して保存、
    // 期限を過ぎたら理由を出して止める。タブ非表示のときも MainComponent の
    // timerTickBackground() 経由で必ず評価される（鳴りっぱなしを防ぐ）
    if (! loopbackRanging)
        return;

    eeq::dsp::SpectrumFrame rangingFrame;
    ctx.engine.getMeasurement().copyLatestResult (rangingFrame);

    if (rangingFrame.averages >= 16)
    {
        finishLoopbackMeasurement();
    }
    else if (juce::Time::getCurrentTime() > loopbackDeadline)
    {
        loopbackRanging = false;
        loopbackMeasureButton.setEnabled (true);
        loopbackMeasureButton.setButtonText (strings::loopbackMeasureButton);
        ctx.engine.stopExcitation();
        ctx.engine.getMeasurement().stopMeasurement();
        loopbackStatusLabel.setText (
            juce::String (u8"測定できませんでした（入力に信号が届いていません。出力→入力の接続と入力chの割り当てを確認してください）"),
            juce::dontSendNotification);
        eeq::log::write ("loopback: deadline reached with averages="
                         + juce::String (rangingFrame.averages));
    }
}

void DelayIrTab::timerTickBackground()
{
    // 非表示タブ用の軽量ティック（issue #55）: ループバック測定の進行・自動停止だけを行う。
    // 励起を伴う処理がタブを離れたことで止まらず、ピンクノイズが鳴り続けることを防ぐ
    processLoopbackRanging();
}

void DelayIrTab::rebuildSystemList()
{
    systemList.updateContent();
}

void DelayIrTab::refreshChannelComboChoices()
{
    // 開かれている出力chから選択肢を作る（issue #15）
    juce::Array<int> indices;
    juce::StringArray names;
    ctx.engine.getOpenedOutputChannels (indices, names);

    const auto currentId = systemChannelCombo.getSelectedId();
    systemChannelCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < indices.size(); ++i)
        systemChannelCombo.addItem (names[i], indices[i] + 1);

    if (currentId > 0)
        systemChannelCombo.setSelectedId (currentId, juce::dontSendNotification);

    seenDeviceGenerationForChannels = ctx.engine.getDeviceGeneration();
}

void DelayIrTab::refreshSystemEditors()
{
    const auto row = systemList.getSelectedRow();
    auto& systems = ctx.project.currentVenue().systems;

    const auto enabled = row >= 0 && row < (int) systems.size();
    systemNameEditor.setEnabled (enabled);
    systemChannelCombo.setEnabled (enabled);

    if (! enabled)
    {
        systemNameEditor.setText ({}, juce::dontSendNotification);
        return;
    }

    systemNameEditor.setText (systems[(size_t) row].name, juce::dontSendNotification);

    if (systemChannelCombo.getNumItems() == 0)
        refreshChannelComboChoices();

    systemChannelCombo.setSelectedId (systems[(size_t) row].outputChannel + 1,
                                      juce::dontSendNotification);
}

void DelayIrTab::setExciteTarget (int index)
{
    exciteTarget = index;
    auto& systems = ctx.project.currentVenue().systems;
    uint64_t mask = 0;

    if (index < 0)
    {
        // 全体（合成）: 全系統を鳴らす（仕様 3.3.4）
        for (const auto& system : systems)
            mask |= (uint64_t) 1 << juce::jlimit (0, 63, system.outputChannel);

        ctx.state.currentSystemName = strings::systemsAll;
    }
    else if (index < (int) systems.size())
    {
        mask = (uint64_t) 1 << juce::jlimit (0, 63, systems[(size_t) index].outputChannel);
        ctx.state.currentSystemName = systems[(size_t) index].name;
    }

    if (mask != 0)
        ctx.engine.setOutputRoutingMask (mask);

    systemList.updateContent();
    eeq::log::write (juce::String ("excitation target: ") + (index < 0 ? "all systems" : juce::String (index)));
}

void DelayIrTab::saveSystemsToVenue()
{
    // 会場メモ（6.4）と系統は共通なので、変更のたびに保存フラグを立てる程度でよい
    ctx.state.snapshotVersion.fetch_add (1);
}

void DelayIrTab::updateCompare()
{
    auto& snapshots = ctx.project.snapshots();

    const auto indexA = compareComboA.getSelectedId() - 1;
    const auto indexB = compareComboB.getSelectedId() - 1;

    if (indexA < 0 || indexB < 0 || indexA >= (int) snapshots.size() || indexB >= (int) snapshots.size())
    {
        compareResultLabel.setText (strings::compareNoSnapshots, juce::dontSendNotification);
        compareTowerLabel.setText ({}, juce::dontSendNotification);
        comparePolarityLabel.setText ({}, juce::dontSendNotification);
        compareCrossoverLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto& snapshotA = snapshots[(size_t) indexA]; // 基準（メイン）
    const auto& snapshotB = snapshots[(size_t) indexB]; // 比較（サブ/ディレイ/モニター）

    const auto irA = eeq::dsp::buildImpulseResponse (snapshotA.frame);
    const auto irB = eeq::dsp::buildImpulseResponse (snapshotB.frame);
    const auto sampleRate = snapshotA.frame.sampleRate;

    // 到達時間差（Aから見てBが遅れる量。ms）
    const auto arrivalDiffSamples = irB.peakTimeSamples - irA.peakTimeSamples;
    const auto arrivalDiffMs = eeq::acoustics::delayMsForSamples (arrivalDiffSamples, sampleRate);
    const auto temperature = ctx.state.airTemperatureC.load();
    const auto arrivalDiffMetres = eeq::acoustics::distanceMetresForDelay (arrivalDiffMs, temperature);

    // 微調整（既定 +0 ms。仕様 3.3.1）
    const auto fineMs = compareFineSlider.getValue();
    const auto proposedMs = arrivalDiffMs + fineMs;

    // 提案（どちらに遅延を入れるか）
    // 到達を揃えるには「早い側」を遅らせる（遅い側に足すと差が拡大する）。仕様 3.3.1
    juce::String direction;

    if (arrivalDiffSamples > 0.5)        // B が遅い → A を遅らせる
        direction = snapshotA.name;
    else if (arrivalDiffSamples < -0.5)  // B が早い → B を遅らせる
        direction = snapshotB.name;
    else
        direction = juce::String (u8"（一致しています）");

    compareResultLabel.setText (strings::compareArrivalDifference + ": "
                                    + juce::String (arrivalDiffMs, 2) + " ms / "
                                    + juce::String (arrivalDiffSamples, 1) + " samples / "
                                    + juce::String (arrivalDiffMetres, 2) + " m / "
                                    + juce::String (arrivalDiffMetres * 3.28084, 1) + " ft\n"
                                    + strings::compareSuggestedDelay + ": " + direction + juce::String (u8" に +")
                                    + juce::String (std::abs (proposedMs), 2) + " ms"
                                    + (std::abs (fineMs) > 0.05 ? juce::String (u8"（微調整 +") + juce::String (fineMs, 1) + juce::String (u8"ms）") : "")
                                    + "\n" + strings::compareMicPointNote,
                                juce::dontSendNotification);

    // ディレイタワー用の大きな1行表示（仕様 3.3.1）
    compareTowerLabel.setText (juce::String (u8"ディレイタワー：") + direction
                                   + juce::String (u8" に +") + juce::String (std::abs (proposedMs), 1)
                                   + juce::String (u8" ms delay をかけてください"),
                               juce::dontSendNotification);

    // ---- 極性の提案（仕様 3.3.3）----
    // クロスオーバー付近で複素加算し、「そのまま」と「反転」で振幅が持ち上がる方を推奨
    auto& venue = ctx.project.currentVenue();
    double crossoverHz = venue.subCrossoverHz;

    // 未入力のときは、メインとサブのレベル差が6 dB以内の帯域から交差を推定（仕様 3.3.3）
    if (crossoverHz <= 0.0)
    {
        const auto halfBins = (int) std::min (snapshotA.frame.magnitudeDb.size(), snapshotB.frame.magnitudeDb.size());
        const auto binWidth = sampleRate / (double) snapshotA.frame.fftSize;
        double bestDiff = 1.0e9;
        int bestBin = -1;

        for (int k = 1; k < halfBins; ++k)
        {
            const auto freq = (double) k * binWidth;

            if (freq < 40.0 || freq > 500.0)
                continue;

            const auto diff = std::abs (snapshotA.frame.magnitudeDb[(size_t) k]
                                      - snapshotB.frame.magnitudeDb[(size_t) k]);

            if (diff < 6.0 && diff < bestDiff)
            {
                bestDiff = diff;
                bestBin = k;
            }
        }

        if (bestBin > 0)
        {
            crossoverHz = (double) bestBin * binWidth;
            compareCrossoverLabel.setText (strings::compareCrossover + juce::String (u8": 約 ")
                                               + juce::String (crossoverHz, 0) + juce::String (u8" Hz（測定カーブの交差から推定。会場メモに入力するとそちらを優先）"),
                                           juce::dontSendNotification);
        }
        else
        {
            compareCrossoverLabel.setText (strings::compareCrossover + ": " + juce::String (u8"推定できませんでした"), juce::dontSendNotification);
        }
    }
    else
    {
        compareCrossoverLabel.setText (strings::compareCrossover + ": " + juce::String (crossoverHz, 0) + juce::String (u8" Hz（会場メモの設定値）"),
                                       juce::dontSendNotification);
    }

    if (crossoverHz > 0.0)
    {
        // スナップショットごとに自分の binWidth を使う（FFTサイズ/レートが違っても安全に）
        const auto binWidthA = snapshotA.frame.sampleRate / (double) snapshotA.frame.fftSize;
        const auto binWidthB = snapshotB.frame.sampleRate / (double) snapshotB.frame.fftSize;

        // 片方でも実データがなければ極性の推定はできない
        if (snapshotA.frame.transfer.size() < 2 || snapshotB.frame.transfer.size() < 2)
        {
            comparePolarityLabel.setText (strings::comparePolarity + ": " + juce::String (u8"データ不足で推定できません"),
                                          juce::dontSendNotification);
            return;
        }

        auto sumAt = [&] (bool inverted)
        {
            double sum = 0.0;
            int count = 0;

            // クロスオーバー ±1/2 オクターブで合成振幅を予測
            for (double f = crossoverHz / std::sqrt (2.0); f <= crossoverHz * std::sqrt (2.0); f *= std::pow (2.0, 1.0 / 12.0))
            {
                const auto ka = juce::jlimit (1, (int) snapshotA.frame.transfer.size() - 1, (int) std::round (f / binWidthA));
                const auto kb = juce::jlimit (1, (int) snapshotB.frame.transfer.size() - 1, (int) std::round (f / binWidthB));

                const std::complex<double> ha (snapshotA.frame.transfer[(size_t) ka].real(),
                                               snapshotA.frame.transfer[(size_t) ka].imag());
                std::complex<double> hb (snapshotB.frame.transfer[(size_t) kb].real(),
                                         snapshotB.frame.transfer[(size_t) kb].imag());

                if (inverted)
                    hb = -hb;

                sum += std::abs (ha + hb);
                ++count;
            }

            return count > 0 ? sum / count : 0.0;
        };

        const auto normalSum = sumAt (false);
        const auto invertedSum = sumAt (true);

        const auto recommendInvert = invertedSum > normalSum * 1.15; // 15%以上持ち上がるときだけ提案
        comparePolarityLabel.setText (juce::String (strings::comparePolarity) + ": "
                                          + (recommendInvert ? strings::comparePolarityInvert
                                                             : strings::comparePolarityKeep),
                                      juce::dontSendNotification);
    }
}

void DelayIrTab::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    auto left = bounds.removeFromLeft (bounds.getWidth() * 3 / 5);
    auto right = bounds;

    // 左: IRグラフ + 遅延パネル
    irGraphHeight = juce::jlimit (120, 240, left.getHeight() / 3);
    auto irArea = left.removeFromTop (irGraphHeight);
    irTitle.setBounds (irArea.removeFromTop (18));
    irExportButton.setBounds (irArea.removeFromTop (26).removeFromRight (170));

    auto toggles = irArea.removeFromBottom (20);
    etcToggle.setBounds (toggles.removeFromLeft (160));
    peakAlignToggle.setBounds (toggles.removeFromLeft (190));
    rawPhaseToggle.setBounds (toggles.removeFromLeft (150));
    juce::ignoreUnused (toggles);

    auto delayPanel = left;
    auto delayRow = delayPanel.removeFromTop (40);
    delayTitle.setBounds (delayRow.removeFromLeft (50));
    delayValueLabel.setBounds (delayRow);
    measureButton.setBounds (delayPanel.removeFromTop (26).removeFromLeft (110));
    applyButton.setBounds (delayPanel.removeFromTop (26).removeFromLeft (110));
    clearButton.setBounds (delayPanel.removeFromTop (26).removeFromLeft (110));
    phatToggle.setBounds (delayPanel.removeFromTop (22).removeFromLeft (120));

    // 手動入力（連動欄）
    auto manualRow = delayPanel.removeFromTop (24);
    manualDelayTitle.setBounds (manualRow.removeFromLeft (90));
    manualDelayEditor.setBounds (manualRow.removeFromLeft (80));
    manualRow.removeFromLeft (8);
    manualDistanceTitle.setBounds (manualRow.removeFromLeft (70));
    manualDistanceEditor.setBounds (manualRow.removeFromLeft (80));
    manualApplyButton.setBounds (delayPanel.removeFromTop (26).removeFromLeft (130));
    manualWarnLabel.setBounds (delayPanel.removeFromTop (30));
    distanceCheckLabel.setBounds (delayPanel.removeFromTop (30));
    delayProposalLabel.setBounds (delayPanel);

    // 右: ループバック / 系統 / 比較
    auto loopback = right.removeFromTop (86);
    loopbackTitle.setBounds (loopback.removeFromTop (18));
    // 右端から固定幅で確保して潰れを防ぐ（issue #28）
    auto loopbackRow = loopback.removeFromTop (26);
    loopbackEnableToggle.setBounds (loopbackRow.removeFromRight (120));
    loopbackRow.removeFromRight (12);
    loopbackClearButton.setBounds (loopbackRow.removeFromRight (90));
    loopbackRow.removeFromRight (6);
    loopbackMeasureButton.setBounds (loopbackRow);
    loopbackStatusLabel.setBounds (loopback);

    auto systems = right.removeFromTop (178);
    systemsTitle.setBounds (systems.removeFromTop (18));
    systemsHint.setBounds (systems.removeFromTop (14));
    auto systemsButtons = systems.removeFromTop (26);
    systemsRemoveButton.setBounds (systemsButtons.removeFromRight (56));
    systemsButtons.removeFromRight (6);
    systemsAddButton.setBounds (systemsButtons.removeFromRight (100));
    systemsButtons.removeFromRight (6);
    systemsAllButton.setBounds (systemsButtons.removeFromRight (110));
    systems.removeFromTop (4);

    // 系統名と出力chの編集行（issue #15）。chコンボは右端に固定で確保する
    auto systemEditRow = systems.removeFromTop (24);
    systemChannelCombo.setBounds (systemEditRow.removeFromRight (90));
    systemEditRow.removeFromRight (4);
    systemNameEditor.setBounds (systemEditRow);

    systems.removeFromTop (2);
    systemList.setBounds (systems);

    auto compare = right;
    compareTitle.setBounds (compare.removeFromTop (18));
    // A/B を1行に詰めると右カラム幅に収まらず B 側コンボの幅が 0 になるため2行に分ける
    auto compareRowA = compare.removeFromTop (26);
    compareARowLabel.setBounds (compareRowA.removeFromLeft (120));
    compareComboA.setBounds (compareRowA);
    compare.removeFromTop (4);
    auto compareRowB = compare.removeFromTop (26);
    compareBRowLabel.setBounds (compareRowB.removeFromLeft (120));
    compareComboB.setBounds (compareRowB);
    compare.removeFromTop (4);
    compareTowerLabel.setBounds (compare.removeFromTop (30));
    compareResultLabel.setBounds (compare.removeFromTop (80));
    comparePolarityLabel.setBounds (compare.removeFromTop (20));
    compareCrossoverLabel.setBounds (compare.removeFromTop (20));
    auto fineRow = compare.removeFromTop (24);
    compareFineLabel.setBounds (fineRow.removeFromLeft (80));
    compareFineSlider.setBounds (fineRow.removeFromLeft (220));
}

} // namespace eeq::ui
