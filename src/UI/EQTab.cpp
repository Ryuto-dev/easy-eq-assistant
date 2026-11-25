#include "EQTab.h"

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

EQTab::EQTab (UiContext& context)
    : ctx (context)
{
    // ---- 入力 ----
    targetTitle.setText (strings::eqTargetLabel, juce::dontSendNotification);
    styleLabel (targetTitle, 12.0f, false);
    addAndMakeVisible (targetTitle);

    targetCombo.addItem (strings::eqTargetFlat, 1);
    targetCombo.addItem (strings::eqTargetTilt, 2);
    targetCombo.addItem (strings::eqTargetCsv, 3);
    targetCombo.setSelectedId (1, juce::dontSendNotification);
    styleCombo (targetCombo);
    targetCombo.onChange = [this] { buildTargetPoints(); };
    addAndMakeVisible (targetCombo);

    tiltTitle.setText (strings::eqTargetTiltLabel, juce::dontSendNotification);
    styleLabel (tiltTitle);
    addAndMakeVisible (tiltTitle);

    styleTextEditor (tiltEditor);
    tiltEditor.setText ("-1.0", juce::dontSendNotification);
    tiltEditor.setInputRestrictions (8, "0123456789.-");
    tiltEditor.onTextChange = [this] { buildTargetPoints(); };
    addAndMakeVisible (tiltEditor);

    styleButton (targetCsvButton);
    targetCsvButton.setButtonText (strings::eqTargetCsv);
    targetCsvButton.onClick = [this] { loadTargetCsv(); };
    addAndMakeVisible (targetCsvButton);

    rangeTitle.setText (strings::eqRangeLabel, juce::dontSendNotification);
    styleLabel (rangeTitle);
    addAndMakeVisible (rangeTitle);

    styleTextEditor (minFreqEditor);
    styleTextEditor (maxFreqEditor);
    minFreqEditor.setText ("40", juce::dontSendNotification);
    maxFreqEditor.setText ("16000", juce::dontSendNotification);
    minFreqEditor.setInputRestrictions (7, "0123456789.");
    maxFreqEditor.setInputRestrictions (7, "0123456789.");
    addAndMakeVisible (minFreqEditor);
    addAndMakeVisible (maxFreqEditor);

    coherenceTitle.setText (strings::eqCoherenceGateLabel, juce::dontSendNotification);
    styleLabel (coherenceTitle);
    addAndMakeVisible (coherenceTitle);

    styleTextEditor (coherenceEditor);
    coherenceEditor.setText ("0.8", juce::dontSendNotification); // 既定 0.8（仕様 4.1）
    coherenceEditor.setInputRestrictions (6, "0123456789.");
    addAndMakeVisible (coherenceEditor);

    maxBandsTitle.setText (strings::eqMaxBandsLabel, juce::dontSendNotification);
    styleLabel (maxBandsTitle);
    addAndMakeVisible (maxBandsTitle);

    styleSlider (maxBandsSlider);
    maxBandsSlider.setRange (1.0, 8.0, 1.0); // 最大 8 帯域（仕様 4.3）
    maxBandsSlider.setValue (8.0, juce::dontSendNotification);
    maxBandsSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
    addAndMakeVisible (maxBandsSlider);

    styleToggle (valleyBoostToggle);
    valleyBoostToggle.setButtonText (strings::eqValleyBoostToggle);
    addAndMakeVisible (valleyBoostToggle);

    styleAccentButton (createButton);
    createButton.setButtonText (strings::eqCreateButton);
    createButton.onClick = [this] { runProposal(); };
    addAndMakeVisible (createButton);

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

    bandFreqEditor.setInputRestrictions (9, "0123456789.");
    bandGainEditor.setInputRestrictions (8, "0123456789.-");
    bandQEditor.setInputRestrictions (8, "0123456789.");

    bandFreqEditor.onTextChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            ctx.state.proposedBands[(size_t) selected].frequency = bandFreqEditor.getText().getDoubleValue();
            updatePrediction();
        }
    };
    bandGainEditor.onTextChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            ctx.state.proposedBands[(size_t) selected].gainDb = bandGainEditor.getText().getDoubleValue();
            updatePrediction();
        }
    };
    bandQEditor.onTextChange = [this]
    {
        const auto selected = bandList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.state.proposedBands.size())
        {
            ctx.state.proposedBands[(size_t) selected].q = bandQEditor.getText().getDoubleValue();
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

    styleButton (exportTextButton);
    exportTextButton.setButtonText (strings::eqExportText);
    exportTextButton.onClick = [this] { exportText(); };
    addAndMakeVisible (exportTextButton);

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
    eeq::dsp::SpectrumFrame frame;
    ctx.engine.getMeasurement().copyLatestResult (frame);

    if (frame.averages == 0)
        return;

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

    // ターゲットを同じグリッドへ
    std::vector<double> targetDb (gridFrequencies.size(), 0.0);

    for (size_t i = 0; i < gridFrequencies.size() && i < ctx.state.targetPoints.size(); ++i)
        targetDb[i] = 20.0 * std::log10 (std::max (std::abs (ctx.state.targetPoints[i].h), 1.0e-9));

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

    // 提案を共有する（TFタブが予測カーブを重ね描きする。仕様 4.4）
    ctx.state.proposedBands = lastResult.bands;
    updatePrediction();

    convergenceLabel.setText (lastResult.converged ? strings::eqConverged : strings::eqNotConverged,
                              juce::dontSendNotification);
    refreshBandTable();
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
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File())
                return;

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
                targetCombo.setSelectedId (3, juce::dontSendNotification);
                buildTargetPoints();
            }
        });
}

void EQTab::exportCsv()
{
    auto chooser = std::make_shared<juce::FileChooser> ("eq_proposal.csv",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.csv");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File())
                return;

            const auto error = eeq::core::exporters::writeEQProposalCsv (ctx.state.proposedBands, file);

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
        });
}

void EQTab::exportText()
{
    auto chooser = std::make_shared<juce::FileChooser> ("eq_proposal.txt",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File())
                return;

            const auto error = eeq::core::exporters::writeEQProposalText (ctx.state.proposedBands, file);

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                    strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
        });
}

void EQTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));
}

void EQTab::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    auto left = bounds.removeFromLeft (bounds.getWidth() * 2 / 5);
    auto right = bounds;

    // 左: 入力パラメータ
    targetTitle.setBounds (left.removeFromTop (18));
    auto targetRow = left.removeFromTop (26);
    targetCombo.setBounds (targetRow.removeFromLeft (150));
    targetRow.removeFromLeft (6);
    tiltTitle.setBounds (targetRow.removeFromLeft (100));
    tiltEditor.setBounds (targetRow.removeFromLeft (60));
    targetRow.removeFromLeft (6);
    targetCsvButton.setBounds (targetRow.removeFromLeft (110));

    left.removeFromTop (6);
    auto rangeRow = left.removeFromTop (26);
    rangeTitle.setBounds (rangeRow.removeFromLeft (110));
    minFreqEditor.setBounds (rangeRow.removeFromLeft (70));
    rangeRow.removeFromLeft (6);
    maxFreqEditor.setBounds (rangeRow.removeFromLeft (80));

    left.removeFromTop (6);
    auto gateRow = left.removeFromTop (26);
    coherenceTitle.setBounds (gateRow.removeFromLeft (190));
    coherenceEditor.setBounds (gateRow.removeFromLeft (60));
    gateRow.removeFromLeft (6);
    maxBandsTitle.setBounds (gateRow.removeFromLeft (110));
    maxBandsSlider.setBounds (gateRow);

    left.removeFromTop (6);
    auto boostRow = left.removeFromTop (26);
    valleyBoostToggle.setBounds (boostRow.removeFromLeft (220));
    boostRow.removeFromLeft (6);
    createButton.setBounds (boostRow);

    left.removeFromTop (8);
    convergenceLabel.setBounds (left.removeFromTop (18));

    // 右: 帯域一覧と手動調整
    bandTitle.setBounds (right.removeFromTop (18));
    auto tableHeight = right.getHeight() - 120;
    bandList.setBounds (right.removeFromTop (std::max (60, tableHeight)));
    right.removeFromTop (4);
    selectedLabel.setBounds (right.removeFromTop (16));

    auto editRow1 = right.removeFromTop (26);
    bandTypeCombo.setBounds (editRow1.removeFromLeft (110));
    editRow1.removeFromLeft (6);
    bandFreqEditor.setBounds (editRow1.removeFromLeft (80));
    editRow1.removeFromLeft (4);
    bandGainEditor.setBounds (editRow1.removeFromLeft (70));
    editRow1.removeFromLeft (4);
    bandQEditor.setBounds (editRow1.removeFromLeft (60));
    editRow1.removeFromLeft (8);
    bandEnableToggle.setBounds (editRow1.removeFromLeft (90));

    auto exportRow = right.removeFromTop (26);
    exportCsvButton.setBounds (exportRow.removeFromLeft (120));
    exportRow.removeFromLeft (6);
    exportTextButton.setBounds (exportRow.removeFromLeft (140));
}

} // namespace eeq::ui
