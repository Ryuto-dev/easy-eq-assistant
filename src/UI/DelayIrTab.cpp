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

    delayValueLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    delayValueLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
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
            return;

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
        system.outputChannel = 0;
        system.colour = theme::get ((theme::ColourId) ((int) theme::ColourId::snapshotPalette1
                                                     + systems.size() % paletteCount));
        systems.push_back (system);
        saveSystemsToVenue();
        rebuildSystemList();
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

    compareTowerLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::accent));
    compareTowerLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
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
    // 出力→入力のループバック測定（仕様 6.1）。
    // 現在の平均結果をインターフェース自身の伝達関数として保存する
    eeq::dsp::SpectrumFrame frame;
    ctx.engine.getMeasurement().copyLatestResult (frame);

    if (frame.averages == 0)
        return;

    // 往復遅延も検出して保存する（後の測定で差し引ける）
    const auto estimate = ctx.engine.getMeasurement().estimateCurrentDelay (false);

    ctx.correction.setCorrection (frame, estimate.valid ? estimate.delaySamples : 0.0);
    eeq::log::write (juce::String ("loopback correction measured: delay=")
                     + juce::String (estimate.valid ? estimate.delaySamples : 0.0, 2) + " samples, averages="
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

void DelayIrTab::rebuildSystemList()
{
    systemList.updateContent();
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
    manualWarnLabel.setBounds (delayPanel.removeFromTop (30));
    distanceCheckLabel.setBounds (delayPanel.removeFromTop (30));
    delayProposalLabel.setBounds (delayPanel);

    // 右: ループバック / 系統 / 比較
    auto loopback = right.removeFromTop (86);
    loopbackTitle.setBounds (loopback.removeFromTop (18));
    auto loopbackRow = loopback.removeFromTop (26);
    loopbackMeasureButton.setBounds (loopbackRow.removeFromLeft (170));
    loopbackRow.removeFromLeft (6);
    loopbackClearButton.setBounds (loopbackRow.removeFromLeft (90));
    loopbackRow.removeFromLeft (12);
    loopbackEnableToggle.setBounds (loopbackRow.removeFromLeft (120));
    loopbackStatusLabel.setBounds (loopback);

    auto systems = right.removeFromTop (150);
    systemsTitle.setBounds (systems.removeFromTop (18));
    systemsHint.setBounds (systems.removeFromTop (14));
    auto systemsButtons = systems.removeFromTop (26);
    systemsAllButton.setBounds (systemsButtons.removeFromLeft (110));
    systemsButtons.removeFromLeft (6);
    systemsAddButton.setBounds (systemsButtons.removeFromLeft (100));
    systemsButtons.removeFromLeft (6);
    systemsRemoveButton.setBounds (systemsButtons.removeFromLeft (56));
    systems.removeFromTop (4);
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
