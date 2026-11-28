#include "EQTab.h"

#include "ApplyValues.h"

#include "../Core/Exporters.h"
#include "../Core/Log.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

namespace
{
// 1/12オクターブのグリッド（補正EQ案の計算用。仕様 4.1 はスムージング1/6 oct を既定とする）
std::vector<double> makeEqGrid (double fMin, double fMax)
{
    std::vector<double> grid;
    const auto steps = (int) std::floor (std::log2 (fMax / fMin) * 12.0);

    for (int i = 0; i <= steps; ++i)
        grid.push_back (fMin * std::pow (2.0, (double) i / 12.0));

    return grid;
}
} // namespace

namespace
{
bool parseBandValue (const juce::String& text, double& out)
{
    const auto trimmed = text.trim();

    if (! trimmed.containsAnyOf ("0123456789"))
        return false;

    out = trimmed.getDoubleValue();
    return std::isfinite (out);
}
} // namespace

EQTab::EQTab (UiContext& context)
    : ctx (context)
{
    // ---- 入力 ----
    targetTitle.setText (strings::eqTargetLabel, juce::dontSendNotification);
    styleLabel (targetTitle, 12.0f, false);
    targetTitle.setTooltip (strings::eqTooltipTarget);
    addAndMakeVisible (targetTitle);

    targetCombo.addItem (strings::eqTargetFlat, 1);
    targetCombo.addItem (strings::eqTargetTilt, 2);
    targetCombo.addItem (strings::eqTargetCsv, 3);
    targetCombo.setSelectedId (1, juce::dontSendNotification);
    styleCombo (targetCombo);
    targetCombo.onChange = [this] { buildTargetPoints(); };
    targetCombo.setTooltip (strings::eqTooltipTarget);
    addAndMakeVisible (targetCombo);

    tiltTitle.setText (strings::eqTargetTiltLabel, juce::dontSendNotification);
    styleLabel (tiltTitle);
    tiltTitle.setTooltip (strings::eqTooltipTarget);
    addAndMakeVisible (tiltTitle);

    styleTextEditor (tiltEditor);
    tiltEditor.setText ("-1.0", juce::dontSendNotification);
    tiltEditor.setInputRestrictions (8, "0123456789.-");
    tiltEditor.onTextChange = [this] { buildTargetPoints(); };
    tiltEditor.setTooltip (strings::eqTooltipTarget);
    addAndMakeVisible (tiltEditor);

    styleButton (targetCsvButton);
    targetCsvButton.setButtonText (strings::eqTargetCsv);
    targetCsvButton.onClick = [this] { loadTargetCsv(); };
    targetCsvButton.setTooltip (strings::eqTooltipTarget);
    addAndMakeVisible (targetCsvButton);

    rangeTitle.setText (strings::eqRangeLabel, juce::dontSendNotification);
    styleLabel (rangeTitle);
    rangeTitle.setTooltip (strings::eqTooltipRange);
    addAndMakeVisible (rangeTitle);

    styleTextEditor (minFreqEditor);
    styleTextEditor (maxFreqEditor);
    minFreqEditor.setText ("40", juce::dontSendNotification);
    maxFreqEditor.setText ("16000", juce::dontSendNotification);
    minFreqEditor.setInputRestrictions (7, "0123456789.");
    maxFreqEditor.setInputRestrictions (7, "0123456789.");
    minFreqEditor.setTooltip (strings::eqTooltipRange);
    maxFreqEditor.setTooltip (strings::eqTooltipRange);

    // 補正範囲を変えたらターゲットカーブも作り直す（グラフの重ね描きと提案の両方が変わる）
    minFreqEditor.onTextChange = [this] { buildTargetPoints(); updateEqGraph(); };
    maxFreqEditor.onTextChange = [this] { buildTargetPoints(); updateEqGraph(); };
    addAndMakeVisible (minFreqEditor);
    addAndMakeVisible (maxFreqEditor);

    coherenceTitle.setText (strings::eqCoherenceGateLabel, juce::dontSendNotification);
    styleLabel (coherenceTitle);
    coherenceTitle.setTooltip (strings::eqTooltipCoherence);
    addAndMakeVisible (coherenceTitle);

    styleTextEditor (coherenceEditor);
    coherenceEditor.setText ("0.8", juce::dontSendNotification); // 既定 0.8（仕様 4.1）
    coherenceEditor.setInputRestrictions (6, "0123456789.");
    coherenceEditor.setTooltip (strings::eqTooltipCoherence);
    addAndMakeVisible (coherenceEditor);

    maxBandsTitle.setText (strings::eqMaxBandsLabel, juce::dontSendNotification);
    styleLabel (maxBandsTitle);
    maxBandsTitle.setTooltip (strings::eqTooltipBands);
    addAndMakeVisible (maxBandsTitle);

    styleSlider (maxBandsSlider);
    maxBandsSlider.setRange (1.0, 8.0, 1.0); // 最大 8 帯域（仕様 4.3）
    maxBandsSlider.setValue (8.0, juce::dontSendNotification);
    maxBandsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
    maxBandsSlider.setTooltip (strings::eqTooltipBands);
    addAndMakeVisible (maxBandsSlider);

    styleToggle (valleyBoostToggle);
    valleyBoostToggle.setButtonText (strings::eqValleyBoostToggle);
    valleyBoostToggle.setTooltip (strings::eqTooltipValley);
    addAndMakeVisible (valleyBoostToggle);

    styleAccentButton (createButton);
    createButton.setButtonText (strings::eqCreateButton);
    createButton.onClick = [this] { runProposal(); };
    createButton.setTooltip (strings::eqTooltipCreate);
    addAndMakeVisible (createButton);

    // Issue #149: EQ専用グラフ
    addAndMakeVisible (eqGraph);

    styleLabel (levelAlignLabel);
    levelAlignLabel.setTooltip (strings::eqLevelAlignTooltip);
    addAndMakeVisible (levelAlignLabel);

    // ---- 帯域一覧 ----
    bandTitle.setText (strings::eqSection, juce::dontSendNotification);
    styleLabel (bandTitle, 12.0f, false);
    addAndMakeVisible (bandTitle);

    bandList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    bandList.setRowHeight (22);
    addAndMakeVisible (bandList);

    selectedLabel.setText ({}, juce::dontSendNotification);
    selectedLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    selectedLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (selectedLabel);

    bandTypeCombo.addItem (strings::eqBell, 1);
    bandTypeCombo.addItem (strings::eqLowShelf, 2);
    bandTypeCombo.addItem (strings::eqHighShelf, 3);
    bandTypeCombo.addItem (strings::eqLowPass, 4);
    bandTypeCombo.addItem (strings::eqHighPass, 5);
    styleCombo (bandTypeCombo);
    bandTypeCombo.onChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            auto& band = ctx.state.proposedBands[(size_t) selected];
            band.type = (eeq::dsp::peq::Band::Type) (bandTypeCombo.getSelectedId() - 1);
            updatePrediction();
        }
    };
    addAndMakeVisible (bandTypeCombo);

    for (auto* editor : { &bandFreqEditor, &bandGainEditor, &bandQEditor })
    {
        styleTextEditor (*editor);
        addAndMakeVisible (*editor);
    }

    // 単位が分かるよう、空欄のときに単位を出し、ツールチップでも示す（issue #140）
    bandFreqEditor.setTextToShowWhenEmpty ("Hz", theme::get (theme::ColourId::textDim));
    bandGainEditor.setTextToShowWhenEmpty ("dB", theme::get (theme::ColourId::textDim));
    bandQEditor.setTextToShowWhenEmpty ("Q", theme::get (theme::ColourId::textDim));
    bandFreqEditor.setTooltip (strings::eqBandFreqTooltip);
    bandGainEditor.setTooltip (strings::eqBandGainTooltip);
    bandQEditor.setTooltip (strings::eqBandQTooltip);

    bandFreqEditor.setInputRestrictions (9, "0123456789.");
    bandGainEditor.setInputRestrictions (8, "0123456789.-");
    bandQEditor.setInputRestrictions (8, "0123456789.");

    bandFreqEditor.onTextChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            // 空欄・入力途中の値は反映しない（issue #140: 0 Hz / Q 0 になり予測が NaN になっていた）
            double value = 0.0;

            if (! parseBandValue (bandFreqEditor.getText(), value))
                return;

            ctx.state.proposedBands[(size_t) selected].frequency = juce::jlimit (10.0, ctx.engine.getCurrentSampleRate() * 0.45, value);
            updatePrediction();
        }
    };
    bandGainEditor.onTextChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            // 空欄・入力途中の値は反映しない（issue #140: 0 Hz / Q 0 になり予測が NaN になっていた）
            double value = 0.0;

            if (! parseBandValue (bandGainEditor.getText(), value))
                return;

            ctx.state.proposedBands[(size_t) selected].gainDb = juce::jlimit (-24.0, 24.0, value);
            updatePrediction();
        }
    };
    bandQEditor.onTextChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            // 空欄・入力途中の値は反映しない（issue #140: 0 Hz / Q 0 になり予測が NaN になっていた）
            double value = 0.0;

            if (! parseBandValue (bandQEditor.getText(), value))
                return;

            ctx.state.proposedBands[(size_t) selected].q = juce::jlimit (0.1, 30.0, value);
            updatePrediction();
        }
    };

    styleToggle (bandEnableToggle);
    bandEnableToggle.setButtonText ("ON/OFF");
    bandEnableToggle.onClick = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            ctx.state.proposedBands[(size_t) selected].enabled = bandEnableToggle.getToggleState();
            updatePrediction();
        }
    };
    addAndMakeVisible (bandEnableToggle);

    convergenceLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (convergenceLabel);

    styleButton (exportCsvButton);
    exportCsvButton.setButtonText (strings::eqExportCsv);
    exportCsvButton.onClick = [this] { exportCsv(); };
    addAndMakeVisible (exportCsvButton);
    exportCsvButton.setEnabled (! ctx.state.proposedBands.empty());

    styleButton (exportTextButton);
    exportTextButton.setButtonText (strings::eqExportText);
    exportTextButton.onClick = [this] { exportText(); };
    addAndMakeVisible (exportTextButton);
    exportTextButton.setEnabled (! ctx.state.proposedBands.empty());

    // 「入れる値」へ記録（2026-11 リデザイン）: 測定中の系統の EQ として残す
    styleButton (recordButton, {}, "accent");
    recordButton.setButtonText (strings::applyRecordEq);
    recordButton.setTooltip (strings::applyRecordEqTooltip);
    recordButton.onClick = [this]
    {
        applyvalues::recordEq (ctx, ctx.state.currentSystemName, ctx.state.proposedBands);
        recordButton.setButtonText (strings::applyRecordedShort);
    };
    addAndMakeVisible (recordButton);
    recordButton.setEnabled (! ctx.state.proposedBands.empty());

    buildTargetPoints();
}

void EQTab::timerTick()
{
    // EQ案はユーザー操作時だけ計算する（仕様 2. の (4)）。ここでは測定の進行だけを受け取る
    juce::ignoreUnused (ctx);
}

void EQTab::buildTargetPoints()
{
    // ターゲット曲線（仕様 4.1）: フラット（既定）/ ユーザー定義（CSV）/ 高域傾斜
    const auto fMin = std::max (20.0, minFreqEditor.getText().getDoubleValue());
    const auto fMax = std::min (20000.0, maxFreqEditor.getText().getDoubleValue());

    if (! (fMax > fMin * 1.01))
        return;

    ctx.state.targetPoints.clear();
    const auto targetId = targetCombo.getSelectedId();

    if (targetId == 3 && ! csvTargetPoints.empty())
    {
        // CSVの点をグリッドへ線形補間
        for (const auto freq : makeEqGrid (fMin, fMax))
        {
            double db = 0.0;
            bool found = false;

            for (size_t i = 1; i < csvTargetPoints.size(); ++i)
            {
                if (freq <= csvTargetPoints[i].first)
                {
                    const auto& a = csvTargetPoints[(size_t) (i - 1)];
                    const auto& b = csvTargetPoints[i];
                    const auto t = (freq - a.first) / std::max (1.0e-9, b.first - a.first);
                    db = a.second + (b.second - a.second) * t;
                    found = true;
                    break;
                }
            }

            if (! found)
                db = csvTargetPoints.back().second;

            eeq::dsp::GridPoint point;
            point.freq = freq;
            point.h = std::complex<double> (std::pow (10.0, db / 20.0), 0.0);
            point.coherence = 1.0f;
            ctx.state.targetPoints.push_back (point);
        }
    }
    else if (targetId == 2)
    {
        // 高域を緩く下げる傾斜（dB/oct 指定。仕様 4.1）
        const auto tilt = tiltEditor.getText().getDoubleValue();

        for (const auto freq : makeEqGrid (fMin, fMax))
        {
            const auto db = tilt * std::log2 (freq / 1000.0);
            eeq::dsp::GridPoint point;
            point.freq = freq;
            point.h = std::complex<double> (std::pow (10.0, db / 20.0), 0.0);
            point.coherence = 1.0f;
            ctx.state.targetPoints.push_back (point);
        }
    }
    else
    {
        // フラット
        for (const auto freq : makeEqGrid (fMin, fMax))
        {
            eeq::dsp::GridPoint point;
            point.freq = freq;
            point.h = { 1.0, 0.0 };
            point.coherence = 1.0f;
            ctx.state.targetPoints.push_back (point);
        }
    }

    ctx.state.eqVersion.fetch_add (1);
}

void EQTab::runProposal()
{
    // 補正範囲を変えたあとでもターゲットが必ず現在の範囲で作られるようにする
    buildTargetPoints();

    eeq::dsp::SpectrumFrame frame;
    ctx.engine.getMeasurement().copyLatestResult (frame);

    if (frame.averages < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            strings::eqSection,
            juce::String (u8"測定結果がありません。測定してから EQ 案を作成してください。"),
            strings::okButton, this, nullptr);
        return;
    }

    // システム補正が有効なら補正後のカーブに対してEQ案を作る（仕様 6.1）
    if (ctx.correction.isActive() && ctx.state.loopbackEnabled.load() && ctx.correction.matches (frame))
    {
        eeq::dsp::SpectrumFrame corrected;
        ctx.correction.apply (frame, corrected, true);
        frame = corrected;
    }

    const auto fMin = std::max (20.0, minFreqEditor.getText().getDoubleValue());
    const auto fMax = std::min (frame.sampleRate * 0.45, maxFreqEditor.getText().getDoubleValue());

    if (! (fMax > fMin * 1.01))
        return;

    // 測定カーブ: スムージング（既定 1/6 oct）後の H(f)（仕様 4.1）
    gridFrequencies.clear();
    measuredDb.clear();
    weight.clear();

    const auto smoothed = eeq::dsp::smoothToLogGrid (frame, ctx.state.smoothingDenom > 0 ? ctx.state.smoothingDenom : 6,
                                                     fMin, fMax, (int) makeEqGrid (fMin, fMax).size());
    const auto coherenceThreshold = coherenceEditor.getText().getDoubleValue();

    for (const auto& point : smoothed)
    {
        gridFrequencies.push_back (point.freq);
        measuredDb.push_back (20.0 * std::log10 (std::max (std::abs (point.h), 1.0e-9)));

        // コヒーレンスがしきい値未満の帯域は誤差を0として扱う（仕様 4.1）
        weight.push_back (point.coherence >= (float) coherenceThreshold ? 1.0 : 0.0);
    }

    // ターゲットを同じグリッドへ（グリッドは buildTargetPoints と同じ式なので
    // 点数がそろっていればインデックス対応で一致する。ずれる場合は対数補間で受ける）
    std::vector<double> targetDb (gridFrequencies.size(), 0.0);

    if ((int) ctx.state.targetPoints.size() == (int) gridFrequencies.size())
    {
        for (size_t i = 0; i < gridFrequencies.size(); ++i)
            targetDb[i] = 20.0 * std::log10 (std::max (std::abs (ctx.state.targetPoints[i].h), 1.0e-9));
    }
    else
    {
        // 対数周波数で線形補間
        for (size_t i = 0; i < gridFrequencies.size(); ++i)
        {
            const auto freq = gridFrequencies[i];
            double db = 0.0;
            bool found = false;

            for (size_t t = 1; t < ctx.state.targetPoints.size(); ++t)
            {
                if (freq <= ctx.state.targetPoints[t].freq)
                {
                    const auto& a = ctx.state.targetPoints[(size_t) (t - 1)];
                    const auto& b = ctx.state.targetPoints[t];
                    const auto ratio = (freq - a.freq) / std::max (1.0e-9, b.freq - a.freq);
                    db = 20.0 * std::log10 (std::max (std::abs (a.h), 1e-9))
                       + ratio * (20.0 * std::log10 (std::max (std::abs (b.h), 1e-9))
                                - 20.0 * std::log10 (std::max (std::abs (a.h), 1e-9)));
                    found = true;
                    break;
                }
            }

            if (! found && ! ctx.state.targetPoints.empty())
                db = 20.0 * std::log10 (std::max (std::abs (ctx.state.targetPoints.back().h), 1e-9));

            targetDb[i] = db;
        }
    }

    eeq::dsp::EQProposalInput input;
    input.frequency = gridFrequencies;
    input.measuredDb = measuredDb;
    input.targetDb = targetDb;
    input.weight = weight;
    input.sampleRate = frame.sampleRate;
    input.minFrequency = fMin;
    input.maxFrequency = fMax;
    input.maxBands = juce::jlimit (1, 8, (int) maxBandsSlider.getValue());
    input.allowValleyBoost = valleyBoostToggle.getToggleState();

    lastResult = eeq::dsp::proposeEQ (input);
    hasResult = true;

    // ターゲットは測定の全体レベル（補正範囲内の中央値）に合わせてから比べている（issue #109）。
    // 表示も同じだけずらして、予測とターゲットが同じ基準で重なるようにする
    targetDisplayOffsetDb = lastResult.appliedLevelOffsetDb;
    ctx.state.targetLevelOffsetDb = targetDisplayOffsetDb;
    ctx.state.eqVersion.fetch_add (1);
    levelAlignLabel.setText (strings::eqLevelAlignedPrefix + (targetDisplayOffsetDb >= 0 ? "+" : "")
                                 + juce::String (targetDisplayOffsetDb, 1) + " dB",
                             juce::dontSendNotification);

    // 提案を共有する（TFタブが予測カーブを重ね描きする。仕様 4.4）
    ctx.state.proposedBands = lastResult.bands;
    updatePrediction();

    convergenceLabel.setText (lastResult.converged ? strings::eqConverged : strings::eqNotConverged,
                              juce::dontSendNotification);

    // 一覧を作り直すので選択と編集欄を同期させる（古い提案の値が別の帯域に
    // 書き込まれる事故を防ぐ）。編集ハンドラは getSelectedRow() で対象を決める
    // ため、行選択も必ず連動させる（選択が無いと入力が無言で無効になる）
    refreshBandTable();

    if (! ctx.state.proposedBands.empty())
    {
        bandList.selectRow (0);
        selectBandForEditing (0);
    }
    else
    {
        bandList.deselectAllRows();
        selectedLabel.setText ({}, juce::dontSendNotification);
    }

    const auto canExport = ! ctx.state.proposedBands.empty();
    exportCsvButton.setEnabled (canExport);
    exportTextButton.setEnabled (canExport);
    if (recordButton.isEnabled() != canExport)
        recordButton.setButtonText (strings::applyRecordEq);
    recordButton.setEnabled (canExport);
    updateEqGraph();
}

void EQTab::updatePrediction()
{
    // 予測（補正後）カーブ = 測定 + 提案フィルタの応答（仕様 4.4）
    ctx.state.predictionPoints.clear();

    if (! hasResult)
        return;

    eeq::dsp::peq::Cascade cascade;
    cascade.setBands (ctx.state.proposedBands, ctx.engine.getCurrentSampleRate());

    for (size_t i = 0; i < gridFrequencies.size(); ++i)
    {
        const auto predicted = measuredDb[i] + cascade.magnitudeDbAt (gridFrequencies[i], ctx.engine.getCurrentSampleRate());

        eeq::dsp::GridPoint point;
        point.freq = gridFrequencies[i];
        point.h = std::complex<double> (std::pow (10.0, predicted / 20.0), 0.0);
        point.coherence = 1.0f;
        ctx.state.predictionPoints.push_back (point);
    }

    ctx.state.eqVersion.fetch_add (1);

    // 手動編集が一覧の行表示（Hz/dB/Q/OFF）にも反映されるようにする
    bandList.updateContent();
    updateEqGraph();
}

void EQTab::refreshBandTable()
{
    bandList.updateContent();
}

int EQTab::getNumRows()
{
    return (int) ctx.state.proposedBands.size();
}

void EQTab::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto& bands = ctx.state.proposedBands;

    if (rowNumber < 0 || rowNumber >= (int) bands.size())
        return;

    const auto& band = bands[(size_t) rowNumber];

    g.fillAll (rowIsSelected ? theme::get (theme::ColourId::control)
                             : theme::get (theme::ColourId::panelBackground));

    g.setColour (theme::get (theme::ColourId::text).withAlpha (band.enabled ? 1.0f : 0.4f));
    g.setFont (juce::Font (juce::FontOptions (12.5f)));

    const char* type = "Bell";

    switch (band.type)
    {
        case eeq::dsp::peq::Band::Type::bell: type = "Bell"; break;
        case eeq::dsp::peq::Band::Type::lowShelf: type = "LowShelf"; break;
        case eeq::dsp::peq::Band::Type::highShelf: type = "HighShelf"; break;
        case eeq::dsp::peq::Band::Type::lowPass: type = "LPF"; break;
        case eeq::dsp::peq::Band::Type::highPass: type = "HPF"; break;
    }

    g.drawText (juce::String (rowNumber + 1) + ": " + type, 4, 0, 90, height, juce::Justification::centredLeft);
    g.drawText (juce::String (band.frequency, 1) + " Hz", 94, 0, 90, height, juce::Justification::centredLeft);
    g.drawText ((band.gainDb > 0 ? "+" : juce::String()) + juce::String (band.gainDb, 2) + " dB",
                184, 0, 80, height, juce::Justification::centredLeft);
    g.drawText ("Q " + juce::String (band.q, 2), 264, 0, 60, height, juce::Justification::centredLeft);

    if (! band.enabled)
        g.drawText (u8"OFF", width - 40, 0, 36, height, juce::Justification::centredRight);
}

void EQTab::listBoxItemClicked (int rowNumber, const juce::MouseEvent&)
{
    selectBandForEditing (rowNumber);
}

void EQTab::selectBandForEditing (int rowNumber)
{
    const auto& bands = ctx.state.proposedBands;

    if (rowNumber < 0 || rowNumber >= (int) bands.size())
        return;

    const auto& band = bands[(size_t) rowNumber];
    selectedLabel.setText (juce::String (u8"帯域 ") + juce::String (rowNumber + 1) + juce::String (u8" を編集"),
                           juce::dontSendNotification);

    bandTypeCombo.setSelectedId ((int) band.type + 1, juce::dontSendNotification);
    bandFreqEditor.setText (juce::String (band.frequency, 1), juce::dontSendNotification);
    bandGainEditor.setText (juce::String (band.gainDb, 2), juce::dontSendNotification);
    bandQEditor.setText (juce::String (band.q, 2), juce::dontSendNotification);
    bandEnableToggle.setToggleState (band.enabled, juce::dontSendNotification);
}

void EQTab::loadTargetCsv()
{
    auto chooser = std::make_shared<juce::FileChooser> (strings::eqTargetCsv,
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.csv;*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<EQTab> (this), chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File() || safeThis == nullptr)
                return;

            auto& csvTargetPoints = safeThis->csvTargetPoints;
            csvTargetPoints.clear();
            juce::StringArray lines;
            lines.addLines (file.loadFileAsString());

            for (const auto& line : lines)
            {
                if (line.trim().isEmpty() || line.trimStart().startsWith ("#") || line.trimStart().startsWith ("*"))
                    continue;

                juce::StringArray tokens;
                tokens.addTokens (line, ",;\t", "");

                if (tokens.size() < 2)
                    continue;

                const auto freq = tokens[0].trim().getDoubleValue();
                const auto db = tokens[1].trim().getDoubleValue();

                if (freq > 0.0)
                    csvTargetPoints.push_back ({ freq, db });
            }

            if (! csvTargetPoints.empty())
            {
                safeThis->targetCombo.setSelectedId (3, juce::dontSendNotification);
                safeThis->buildTargetPoints();
            }
        });
}

void EQTab::exportCsv()
{
    if (ctx.state.proposedBands.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            strings::eqSection,
            juce::String (u8"先に EQ 案を作成してください。"),
            strings::okButton, this, nullptr);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> ("eq_proposal.csv",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<EQTab> (this), chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File() || safeThis == nullptr)
                return;

            const auto error = eeq::core::exporters::writeEQProposalCsv (safeThis->ctx.state.proposedBands, file);

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
        });
}

void EQTab::exportText()
{
    if (ctx.state.proposedBands.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            strings::eqSection,
            juce::String (u8"先に EQ 案を作成してください。"),
            strings::okButton, this, nullptr);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> ("eq_proposal.txt",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<EQTab> (this), chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File() || safeThis == nullptr)
                return;

            const auto error = eeq::core::exporters::writeEQProposalText (safeThis->ctx.state.proposedBands, file);

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
        });
}

void EQTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    for (const auto& card : cardRects)
    {
        g.setColour (theme::get (theme::ColourId::raised));
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (theme::get (theme::ColourId::controlOutline));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }
}

void EQTab::resized()
{
    // 2026-11 リデザイン: [条件（ターゲット・範囲・作成）] | [予測グラフ] / [帯域一覧・調整・記録]
    auto bounds = getLocalBounds().reduced (16);
    cardRects.clear();

    auto leftCard = bounds.removeFromLeft (juce::jlimit (320, 400, bounds.getWidth() * 2 / 7));
    bounds.removeFromLeft (16);
    cardRects.push_back (leftCard);
    auto left = leftCard.reduced (16, 12);

    auto row = [&left] (int height) { auto r = left.removeFromTop (height); left.removeFromTop (8); return r; };

    targetTitle.setBounds (row (22));
    auto targetRow = row (30);
    targetCombo.setBounds (targetRow.removeFromLeft (150));
    targetRow.removeFromLeft (8);
    targetCsvButton.setBounds (targetRow);
    auto tiltRow = row (30);
    tiltTitle.setBounds (tiltRow.removeFromLeft (130));
    tiltEditor.setBounds (tiltRow.removeFromLeft (70));
    left.removeFromTop (6);

    auto rangeRow = row (30);
    rangeTitle.setBounds (rangeRow.removeFromLeft (100));
    minFreqEditor.setBounds (rangeRow.removeFromLeft (76));
    rangeRow.removeFromLeft (8);
    maxFreqEditor.setBounds (rangeRow.removeFromLeft (84));

    auto gateRow = row (30);
    coherenceTitle.setBounds (gateRow.removeFromLeft (180));
    coherenceEditor.setBounds (gateRow.removeFromLeft (70));

    auto maxBandsRow = row (30);
    maxBandsTitle.setBounds (maxBandsRow.removeFromLeft (100));
    maxBandsSlider.setBounds (maxBandsRow);

    valleyBoostToggle.setBounds (row (28));
    left.removeFromTop (6);
    createButton.setBounds (row (38));
    convergenceLabel.setBounds (row (20));
    levelAlignLabel.setBounds (row (36));

    // 右上: グラフ
    auto graphCard = bounds.removeFromTop (bounds.getHeight() * 52 / 100);
    bounds.removeFromTop (14);
    cardRects.push_back (graphCard);
    eqGraph.setBounds (graphCard.reduced (10, 8));

    // 右下: 帯域
    auto bandCard = bounds;
    cardRects.push_back (bandCard);
    auto right = bandCard.reduced (16, 12);

    auto exportRow = right.removeFromBottom (32);
    recordButton.setBounds (exportRow.removeFromRight (juce::jmin (170, exportRow.getWidth() / 3)));
    exportRow.removeFromRight (8);
    exportCsvButton.setBounds (exportRow.removeFromLeft (130));
    exportRow.removeFromLeft (8);
    exportTextButton.setBounds (exportRow.removeFromLeft (150));
    right.removeFromBottom (8);

    auto editRow1 = right.removeFromBottom (30);
    bandTypeCombo.setBounds (editRow1.removeFromLeft (120));
    editRow1.removeFromLeft (8);
    bandFreqEditor.setBounds (editRow1.removeFromLeft (86));
    editRow1.removeFromLeft (6);
    bandGainEditor.setBounds (editRow1.removeFromLeft (76));
    editRow1.removeFromLeft (6);
    bandQEditor.setBounds (editRow1.removeFromLeft (66));
    editRow1.removeFromLeft (10);
    bandEnableToggle.setBounds (editRow1.removeFromLeft (100));
    right.removeFromBottom (4);
    selectedLabel.setBounds (right.removeFromBottom (18));
    right.removeFromBottom (4);

    bandTitle.setBounds (right.removeFromTop (22));
    right.removeFromTop (4);
    bandList.setBounds (right);
}

// EQ専用グラフ（issue #149）: EQ案の計算に使った測定カーブ・ターゲット・予測を同じ周波数軸で重ねる
void EQTab::updateEqGraph()
{
    std::vector<FrequencyGraphView::OverlayTrace> traces;

    auto toColumns = [] (const std::vector<eeq::dsp::GridPoint>& points)
    {
        auto columns = std::make_shared<std::vector<FrequencyGraphView::OverlayColumn>>();
        columns->reserve (points.size());

        for (const auto& p : points)
            columns->push_back ({ p.freq, { (float) p.h.real(), (float) p.h.imag() } });

        return std::shared_ptr<const std::vector<FrequencyGraphView::OverlayColumn>> (std::move (columns));
    };

    if (hasResult && gridFrequencies.size() == measuredDb.size())
    {
        auto columns = std::make_shared<std::vector<FrequencyGraphView::OverlayColumn>>();

        for (size_t i = 0; i < gridFrequencies.size(); ++i)
            columns->push_back ({ gridFrequencies[i], { (float) std::pow (10.0, measuredDb[i] / 20.0), 0.0f } });

        FrequencyGraphView::OverlayTrace trace;
        trace.name = strings::eqGraphMeasured;
        trace.colour = theme::get (theme::ColourId::accent);
        trace.thickness = 1.4f;
        trace.columns = std::move (columns);
        traces.push_back (std::move (trace));
    }

    if (! ctx.state.targetPoints.empty())
    {
        FrequencyGraphView::OverlayTrace trace;
        trace.name = strings::eqGraphTarget;
        trace.colour = theme::get (theme::ColourId::ok);
        trace.dashed = true;
        trace.dbOffset = targetDisplayOffsetDb;
        trace.columns = toColumns (ctx.state.targetPoints);
        traces.push_back (std::move (trace));
    }

    if (! ctx.state.predictionPoints.empty())
    {
        FrequencyGraphView::OverlayTrace trace;
        trace.name = strings::eqGraphPrediction;
        trace.colour = theme::get (theme::ColourId::warn);
        trace.columns = toColumns (ctx.state.predictionPoints);
        traces.push_back (std::move (trace));
    }

    eqGraph.setViewMode (ViewMode::ampOnly);
    eqGraph.setOverlayTraces (std::move (traces));
}

} // namespace eeq::ui
