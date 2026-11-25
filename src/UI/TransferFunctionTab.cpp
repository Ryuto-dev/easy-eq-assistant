#include "TransferFunctionTab.h"

#include "../Core/Exporters.h"
#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

namespace
{
void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, theme::get (theme::ColourId::buttonOff));
    button.setColour (juce::TextButton::textColourOffId, theme::get (theme::ColourId::text));
    button.setWantsKeyboardFocus (false);
    button.setMouseClickGrabsKeyboardFocus (false);
}

void styleCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, theme::get (theme::ColourId::control));
    combo.setColour (juce::ComboBox::outlineColourId, theme::get (theme::ColourId::controlOutline));
    combo.setColour (juce::ComboBox::textColourId, theme::get (theme::ColourId::text));
    combo.setColour (juce::ComboBox::arrowColourId, theme::get (theme::ColourId::textDim));
    combo.setWantsKeyboardFocus (false);
}
} // namespace

TransferFunctionTab::TransferFunctionTab (UiContext& context)
    : ctx (context)
{
    addAndMakeVisible (graph);

    snapshotList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    snapshotList.setRowHeight (22);
    addAndMakeVisible (snapshotList);

    styleButton (saveButton);
    saveButton.setButtonText (strings::snapshotSaveButton);
    saveButton.onClick = [this] { showSaveSnapshotDialog(); };
    addAndMakeVisible (saveButton);

    styleButton (deleteButton);
    deleteButton.setButtonText (strings::snapshotDeleteButton);
    deleteButton.onClick = [this]
    {
        const auto selected = snapshotList.getSelectedRow();

        if (selected >= 0 && selected < (int) ctx.project.snapshots().size())
        {
            auto& snapshots = ctx.project.snapshots();
            snapshots.erase (snapshots.begin() + selected);
            ctx.state.snapshotVersion.fetch_add (1);
            snapshotList.deselectAllRows();
            snapshotList.updateContent();

            // 差分表示が消えたスナップショットを参照しないよう張り直す
            computeDiffCurve();
            refreshSnapshotList();
        }
    };
    addAndMakeVisible (deleteButton);

    styleButton (exportCsvButton);
    exportCsvButton.setButtonText (strings::snapshotExportCsv);
    exportCsvButton.onClick = [this]
    {
        const auto selected = snapshotList.getSelectedRow();

        if (selected < 0 || selected >= (int) ctx.project.snapshots().size())
            return;

        auto& snapshot = ctx.project.snapshots()[(size_t) selected];
        auto chooser = std::make_shared<juce::FileChooser> (snapshot.name + ".csv",
                                                            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                            "*.csv");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis = juce::Component::SafePointer<TransferFunctionTab> (this), chooser, selected]
            (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();

                if (file == juce::File() || safeThis == nullptr)
                    return;

                auto& snapshots = safeThis->ctx.project.snapshots();

                if (selected < 0 || selected >= (int) snapshots.size())
                    return;

                const auto error = eeq::core::exporters::writeSnapshotCsv (snapshots[(size_t) selected].frame, file);

                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
            });
    };
    addAndMakeVisible (exportCsvButton);

    styleButton (exportRewButton);
    exportRewButton.setButtonText (strings::snapshotExportRew);
    exportRewButton.onClick = [this]
    {
        const auto selected = snapshotList.getSelectedRow();

        if (selected < 0 || selected >= (int) ctx.project.snapshots().size())
            return;

        auto& snapshot = ctx.project.snapshots()[(size_t) selected];
        auto chooser = std::make_shared<juce::FileChooser> (snapshot.name + ".txt",
                                                            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                            "*.txt");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis = juce::Component::SafePointer<TransferFunctionTab> (this), chooser, selected]
            (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();

                if (file == juce::File() || safeThis == nullptr)
                    return;

                auto& snapshots = safeThis->ctx.project.snapshots();

                if (selected < 0 || selected >= (int) snapshots.size())
                    return;

                const auto error = eeq::core::exporters::writeSnapshotRewText (
                    snapshots[(size_t) selected].frame, snapshots[(size_t) selected].name, file);

                if (error.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                        strings::snapshotExportTitle, error, strings::okButton, nullptr, nullptr);
            });
    };
    addAndMakeVisible (exportRewButton);

    diffLabel.setText (strings::snapshotDiffButton, juce::dontSendNotification);
    diffLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    diffLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (diffLabel);

    for (auto* combo : { &diffComboA, &diffComboB })
    {
        styleCombo (*combo);
        combo->setTextWhenNothingSelected (strings::snapshotSelectA);
        combo->onChange = [this] { computeDiffCurve(); };
        addAndMakeVisible (*combo);
    }

    diffComboB.setTextWhenNothingSelected (strings::snapshotSelectB);

    hintLabel.setText (strings::noSnapshots, juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (hintLabel);

    refreshSnapshotList();
}

void TransferFunctionTab::timerTick()
{
    eeq::dsp::SpectrumFrame frame;
    ctx.engine.getMeasurement().copyLatestResult (frame);

    // システム補正（ループバック校正。仕様 6.1）: 有効ならインターフェース自身の応答を差し引く
    if (ctx.correction.isActive() && ctx.state.loopbackEnabled.load() && ctx.correction.matches (frame))
    {
        eeq::dsp::SpectrumFrame corrected;
        ctx.correction.apply (frame, corrected, true);
        frame = corrected;
    }

    const auto smoothing = ctx.state.smoothingDenom;
    graph.setLiveFrame (frame, smoothing, ctx.state.appliedDelaySamples.load(),
                        ctx.state.showRawPhase.load());

    if (ctx.state.snapshotVersion.load() != lastSeenSnapshotVersion
        || ctx.state.eqVersion.load() != lastSeenEqVersion
        || smoothing != cachedSmoothing)
        refreshSnapshotList();
}

void TransferFunctionTab::refreshSnapshotList()
{
    lastSeenSnapshotVersion = ctx.state.snapshotVersion.load();
    lastSeenEqVersion = ctx.state.eqVersion.load();
    cachedSmoothing = ctx.state.smoothingDenom;

    snapshotList.updateContent();
    updateDiffCombos();
    rebuildOverlays();
    hintLabel.setVisible (ctx.project.snapshots().empty());
}

void TransferFunctionTab::rebuildOverlays()
{
    std::vector<FrequencyGraphView::OverlaySource> sources;
    const auto smoothing = ctx.state.smoothingDenom;

    for (auto& snapshot : ctx.project.snapshots())
    {
        if (! snapshot.visible)
            continue;

        FrequencyGraphView::OverlaySource source;
        source.name = snapshot.name;
        source.colour = snapshot.colour;
        source.frame = &snapshot.frame;
        source.thickness = 1.2f;
        sources.push_back (source);
    }

    // ターゲット曲線（EQ案タブが設定する。仕様 4.1）
    if (! ctx.state.targetPoints.empty())
    {
        FrequencyGraphView::OverlaySource source;
        source.name = strings::eqTargetLabel;
        source.colour = theme::get (theme::ColourId::ok);
        source.points = &ctx.state.targetPoints;
        source.thickness = 1.2f;
        source.dashed = true;
        sources.push_back (source);
    }

    // 予測（補正後）カーブ（仕様 4.4）
    if (! ctx.state.predictionPoints.empty())
    {
        FrequencyGraphView::OverlaySource source;
        source.name = strings::eqPredictionLegend;
        source.colour = theme::get (theme::ColourId::warn);
        source.points = &ctx.state.predictionPoints;
        source.thickness = 1.0f;
        sources.push_back (source);
    }

    // 差分 A−B（仕様 5.2）
    if (! diffPoints.empty())
    {
        FrequencyGraphView::OverlaySource source;
        source.name = strings::snapshotDiffButton;
        source.colour = theme::get (theme::ColourId::text);
        source.points = &diffPoints;
        source.thickness = 1.4f;
        source.dashed = true;
        sources.push_back (source);
    }

    graph.setOverlays (sources, ctx.state.snapshotVersion.load() * 31 + ctx.state.eqVersion.load(),
                       smoothing);
}

void TransferFunctionTab::computeDiffCurve()
{
    auto& snapshots = ctx.project.snapshots();

    // コンボの選択を A/B フラグへ反映する
    for (int i = 0; i < (int) snapshots.size(); ++i)
    {
        snapshots[(size_t) i].isSelectedA = diffComboA.getSelectedId() == i + 1;
        snapshots[(size_t) i].isSelectedB = diffComboB.getSelectedId() == i + 1;
    }

    diffPoints.clear();

    const auto indexA = diffComboA.getSelectedId() - 1;
    const auto indexB = diffComboB.getSelectedId() - 1;

    if (indexA < 0 || indexB < 0 || indexA >= (int) snapshots.size() || indexB >= (int) snapshots.size()
        || indexA == indexB)
    {
        rebuildOverlays();
        return;
    }

    const auto& frameA = snapshots[(size_t) indexA].frame;
    const auto& frameB = snapshots[(size_t) indexB].frame;
    const auto smoothing = ctx.state.smoothingDenom;

    const auto columnsA = eeq::dsp::smoothToLogGrid (frameA, smoothing, 20.0, 20000.0, graph.plotWidth());
    const auto columnsB = eeq::dsp::smoothToLogGrid (frameB, smoothing, 20.0, 20000.0, graph.plotWidth());

    diffPoints.resize (columnsA.size());

    for (size_t i = 0; i < columnsA.size(); ++i)
    {
        const auto ampA = 20.0 * std::log10 (std::max (std::abs (columnsA[i].h), 1e-9));
        const auto ampB = 20.0 * std::log10 (std::max (std::abs (columnsB[i].h), 1e-9));
        const auto diffDb = ampA - ampB;

        diffPoints[i].freq = columnsA[i].freq;
        diffPoints[i].h = std::complex<double> (std::pow (10.0, diffDb / 20.0), 0.0);
        diffPoints[i].coherence = 1.0f;
        diffPoints[i].count = columnsA[i].count;
    }

    rebuildOverlays();
}

void TransferFunctionTab::updateDiffCombos()
{
    auto& snapshots = ctx.project.snapshots();

    auto fill = [&snapshots] (juce::ComboBox& combo, bool preferA)
    {
        combo.clear (juce::dontSendNotification);

        for (int i = 0; i < (int) snapshots.size(); ++i)
            combo.addItem (snapshots[(size_t) i].name, i + 1);

        int selectId = 0;

        for (int i = 0; i < (int) snapshots.size(); ++i)
            if (preferA ? snapshots[(size_t) i].isSelectedA : snapshots[(size_t) i].isSelectedB)
                selectId = i + 1;

        if (selectId == 0 && snapshots.size() >= (preferA ? 1 : 2))
            selectId = (preferA ? 1 : 2);

        if (selectId > 0)
            combo.setSelectedId (selectId, juce::dontSendNotification);
    };

    fill (diffComboA, true);
    fill (diffComboB, false);
}

void TransferFunctionTab::showSaveSnapshotDialog()
{
    if (dialogOpen)
        return;

    dialogOpen = true;

    auto* window = new juce::AlertWindow (strings::snapshotDialogTitle, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", juce::Time::getCurrentTime().formatted ("%H:%M:%S"), strings::snapshotNameLabel);
    window->addTextEditor ("note", {}, strings::venueSnapshotMemo);

    juce::StringArray micPointNames { strings::notSelected };

    for (const auto& point : ctx.project.currentVenue().micPoints)
        micPointNames.add (point.name);

    window->addComboBox ("micPoint", micPointNames, strings::venueMicPoints);

    window->addButton (strings::snapshotSaveButton, 1);
    window->addButton (strings::snapshotCancelButton, 0);

    // deleteWhenDismissed = true: 閉じたら必ず破棄する（放置すると幽霊ダイアログとリークになる）
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, safeWindow = juce::Component::SafePointer<juce::AlertWindow> (window)] (int result)
        {
            dialogOpen = false;

            if (result != 1 || safeWindow == nullptr)
                return;

            eeq::core::Snapshot snapshot;
            snapshot.name = safeWindow->getTextEditorContents ("name");

            if (snapshot.name.isEmpty())
                snapshot.name = juce::Time::getCurrentTime().formatted ("%H:%M:%S");

            snapshot.note = safeWindow->getTextEditorContents ("note");
            snapshot.micPoint = safeWindow->getComboBoxComponent ("micPoint")->getText();
            snapshot.systemName = ctx.state.currentSystemName; // 系統名は自動で付く（仕様 3.3.4）
            snapshot.temperatureC = ctx.state.airTemperatureC.load();
            snapshot.timestamp = juce::Time::getCurrentTime();

            // 既定パレットから色を順に割り当てる
            const int paletteCount = 6;
            snapshot.colour = theme::get ((theme::ColourId) ((int) theme::ColourId::snapshotPalette1
                                                           + ctx.project.snapshots().size() % paletteCount));

            ctx.engine.getMeasurement().copyLatestResult (snapshot.frame);
            snapshot.delaySamples = ctx.state.appliedDelaySamples.load();

            if (snapshot.frame.averages == 0)
                return; // 測定していないときは保存しない

            auto& snapshots = ctx.project.snapshots();
            snapshots.push_back (std::move (snapshot));
            ctx.state.snapshotVersion.fetch_add (1);
            snapshotList.selectRow ((int) snapshots.size() - 1);
            refreshSnapshotList();
        }), true);
}

void TransferFunctionTab::exportGraphPng()
{
    auto chooser = std::make_shared<juce::FileChooser> ("graph.png",
                                                        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                        "*.png");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<TransferFunctionTab> (this), chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();

            if (file == juce::File() || safeThis == nullptr)
                return;

            const auto image = safeThis->graph.createComponentSnapshot (safeThis->graph.getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            png.writeImageToStream (image, stream);
        });
}

int TransferFunctionTab::getNumRows()
{
    return (int) ctx.project.snapshots().size();
}

void TransferFunctionTab::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto& snapshots = ctx.project.snapshots();

    if (rowNumber < 0 || rowNumber >= (int) snapshots.size())
        return;

    const auto& snapshot = snapshots[(size_t) rowNumber];

    g.fillAll (rowIsSelected ? theme::get (theme::ColourId::control)
                             : theme::get (theme::ColourId::panelBackground));

    // 色見本。表示ON/OFFは色の濃さで伝える（仕様 5.5: 色だけで伝えない→薄いときは名前も薄く）
    g.setColour (snapshot.colour.withAlpha (snapshot.visible ? 1.0f : 0.25f));
    g.fillRoundedRectangle (6.0f, (float) height / 2 - 6.0f, 12.0f, 12.0f, 2.0f);

    g.setColour (theme::get (theme::ColourId::text).withAlpha (snapshot.visible ? 1.0f : 0.4f));
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    auto text = snapshot.name;

    if (snapshot.systemName.isNotEmpty())
        text += "  [" + snapshot.systemName + "]";

    if (snapshot.micPoint.isNotEmpty())
        text += " @" + snapshot.micPoint;

    g.drawText (text, 26, 0, width - 30, height, juce::Justification::centredLeft);
}

void TransferFunctionTab::listBoxItemClicked (int rowNumber, const juce::MouseEvent& event)
{
    const auto& snapshots = ctx.project.snapshots();

    if (rowNumber < 0 || rowNumber >= (int) snapshots.size())
        return;

    // 色見本のクリックで表示ON/OFFを切り替える
    if (event.position.x < 20.0f)
    {
        auto& snapshot = ctx.project.snapshots()[(size_t) rowNumber];
        snapshot.visible = ! snapshot.visible;
        ctx.state.snapshotVersion.fetch_add (1);
        computeDiffCurve();
        refreshSnapshotList();
    }
}

void TransferFunctionTab::resized()
{
    auto bounds = getLocalBounds().reduced (6);

    auto snapshotPanel = bounds.removeFromBottom (132);
    snapshotPanel.reduce (4, 4);

    auto buttonColumn = snapshotPanel.removeFromLeft (96);
    saveButton.setBounds (buttonColumn.removeFromTop (26));
    buttonColumn.removeFromTop (4);
    deleteButton.setBounds (buttonColumn.removeFromTop (26));
    buttonColumn.removeFromTop (4);
    exportCsvButton.setBounds (buttonColumn.removeFromTop (26));
    buttonColumn.removeFromTop (4);
    exportRewButton.setBounds (buttonColumn.removeFromTop (26));

    auto diffRow = snapshotPanel.removeFromTop (26);
    diffLabel.setBounds (diffRow.removeFromLeft (70));
    diffRow.removeFromLeft (4);
    diffComboA.setBounds (diffRow.removeFromLeft (160));
    diffRow.removeFromLeft (6);
    diffComboB.setBounds (diffRow.removeFromLeft (160));
    snapshotPanel.removeFromTop (4);

    snapshotList.setBounds (snapshotPanel);

    bounds.removeFromBottom (6);
    graph.setBounds (bounds);
    graphBounds = bounds;
}

} // namespace eeq::ui
