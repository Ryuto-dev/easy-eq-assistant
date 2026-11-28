#include "TransferFunctionTab.h"
#include "SweepSession.h"

#include "../Core/Exporters.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

class TransferFunctionTab::LiveSmoothingWorker final : private juce::Thread
{
public:
    struct Request
    {
        std::uint64_t generation = 0;
        std::shared_ptr<const eeq::dsp::SpectrumFrame> frame;
        int smoothing = 6;
        double viewMin = 20.0;
        double viewMax = 20000.0;
        int plotWidth = 2;
        double appliedDelaySamples = 0.0; // 適用中の全遅延。フレームに織り込み済みの分を引いて平滑化の前に回す（#106/#107）
        bool applyCorrection = false;
        std::uint64_t correctionGeneration = 0;
        eeq::core::SystemCorrection* correction = nullptr;
        std::shared_ptr<const eeq::core::MicCalibration> micCalibration;
    };

    struct Result
    {
        std::uint64_t generation = 0;
        std::shared_ptr<const eeq::dsp::SpectrumFrame> frame;
        std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> points;
        int smoothing = 6;
    };

    LiveSmoothingWorker() : juce::Thread ("Transfer-function smoothing") {}
    ~LiveSmoothingWorker() override { stopThread (-1); }

    void start()
    {
        startThread (juce::Thread::Priority::low);
    }

    void submit (Request request)
    {
        const juce::ScopedLock sl (lock);
        pending = std::move (request);
        hasPending = true;
    }

    bool takeResult (Result& result)
    {
        const juce::ScopedLock sl (lock);

        if (! hasResult)
            return false;

        result = std::move (completed);
        hasResult = false;
        return true;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Request request;
            bool haveRequest = false;

            {
                const juce::ScopedLock sl (lock);

                if (hasPending)
                {
                    request = std::move (pending);
                    hasPending = false;
                    haveRequest = true;
                }
            }

            if (! haveRequest)
            {
                juce::Thread::sleep (4);
                continue;
            }

            if (request.frame == nullptr || threadShouldExit())
                continue;

            auto displayFrame = request.frame;

            if (request.applyCorrection && request.correction != nullptr
                && request.correction->getGeneration() == request.correctionGeneration
                && request.correction->matches (*request.frame))
            {
                auto corrected = std::make_shared<eeq::dsp::SpectrumFrame>();
                request.correction->apply (*request.frame, *corrected, true);

                // 補正変更と処理が競合した結果は、次の要求が来るまで表示しない。
                if (request.correction->getGeneration() == request.correctionGeneration)
                    displayFrame = std::move (corrected);
            }

            if (request.micCalibration != nullptr)
            {
                auto withMic = std::make_shared<eeq::dsp::SpectrumFrame> (*displayFrame);
                request.micCalibration->applyTo (*withMic);
                displayFrame = std::move (withMic);
            }

            auto calculated = std::make_shared<std::vector<eeq::dsp::GridPoint>> (
                eeq::dsp::smoothToLogGrid (*displayFrame, request.smoothing, request.viewMin,
                                           std::min (request.viewMax, displayFrame->sampleRate * 0.5),
                                           request.plotWidth, 80.0f,
                                           displayFrame->residualDelayFor (request.appliedDelaySamples)));

            Result result;
            result.generation = request.generation;
            result.frame = std::move (displayFrame);
            result.points = std::move (calculated);
            result.smoothing = request.smoothing;

            {
                const juce::ScopedLock sl (lock);
                completed = std::move (result);
                hasResult = true;
            }
        }
    }

    juce::CriticalSection lock;
    Request pending;
    Result completed;
    bool hasPending = false;
    bool hasResult = false;
};


// 重ね描き（スナップショット/ターゲット/予測/差分）の平滑化用ワーカー（issue #66 / #128）。
// 要求には不変データ（shared_ptr<const>）だけを入れる。UI がスナップショットを削除・並べ替え・
// 再読込しても、ワーカーが読んでいるデータは寿命が保証される。
class TransferFunctionTab::OverlaySmoothingWorker final : private juce::Thread
{
public:
    struct Item
    {
        juce::String key;
        std::shared_ptr<const eeq::dsp::SpectrumFrame> frame;          // スナップショット
        std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> points; // ターゲット/予測（点列）
        std::shared_ptr<const eeq::dsp::SpectrumFrame> diffB;          // 差分: frame − diffB
        int smoothing = 6;
        double delayCompensationSamples = 0.0;
    };

    struct Request
    {
        std::uint64_t generation = 0;
        std::vector<Item> items;
        double viewMin = 20.0;
        double viewMax = 20000.0;
        int plotWidth = 2;
    };

    using Columns = std::vector<FrequencyGraphView::OverlayColumn>;

    struct Result
    {
        std::uint64_t generation = 0;
        std::map<juce::String, std::shared_ptr<const Columns>> columns;
    };

    OverlaySmoothingWorker() : juce::Thread ("Overlay smoothing") {}
    ~OverlaySmoothingWorker() override { stopThread (-1); }

    void start() { startThread (juce::Thread::Priority::low); }

    void submit (Request request)
    {
        const juce::ScopedLock sl (lock);
        pending = std::move (request);
        hasPending = true;
        notify();
    }

    bool takeResult (Result& result)
    {
        const juce::ScopedLock sl (lock);

        if (! hasResult)
            return false;

        result = std::move (completed);
        hasResult = false;
        return true;
    }

    /** テストからも使う純粋関数: 1 項目ぶんの列を作る */
    static std::shared_ptr<const Columns> compute (const Item& item, double viewMin, double viewMax, int width)
    {
        auto out = std::make_shared<Columns>();

        auto appendPoints = [&out] (const std::vector<eeq::dsp::GridPoint>& points, double lo, double hi)
        {
            out->reserve (points.size());

            for (const auto& point : points)
                if (point.count > 0 && point.freq >= lo && point.freq <= hi)
                    out->push_back ({ point.freq, { (float) point.h.real(), (float) point.h.imag() } });
        };

        if (item.frame != nullptr && item.diffB != nullptr)
        {
            const auto hi = std::min ({ viewMax, item.frame->sampleRate * 0.5, item.diffB->sampleRate * 0.5 });
            const auto a = eeq::dsp::smoothToLogGrid (*item.frame, item.smoothing, viewMin, hi, width);
            const auto b = eeq::dsp::smoothToLogGrid (*item.diffB, item.smoothing, viewMin, hi, width);
            out->reserve (a.size());

            for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            {
                if (a[i].count == 0 || b[i].count == 0)
                    continue;

                const auto diffDb = (double) a[i].magnitudeDb - (double) b[i].magnitudeDb;
                out->push_back ({ a[i].freq, { (float) std::pow (10.0, diffDb / 20.0), 0.0f } });
            }
        }
        else if (item.frame != nullptr)
        {
            const auto hi = std::min (viewMax, item.frame->sampleRate * 0.5);
            appendPoints (eeq::dsp::smoothToLogGrid (*item.frame, item.smoothing, viewMin, hi, width, 80.0f,
                                                     item.delayCompensationSamples),
                          viewMin, hi);
        }
        else if (item.points != nullptr)
        {
            appendPoints (*item.points, viewMin * 0.999, viewMax * 1.001);
        }

        return out;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Request request;
            bool haveRequest = false;

            {
                const juce::ScopedLock sl (lock);

                if (hasPending)
                {
                    request = std::move (pending);
                    hasPending = false;
                    haveRequest = true;
                }
            }

            if (! haveRequest)
            {
                wait (50);
                continue;
            }

            Result result;
            result.generation = request.generation;

            for (const auto& item : request.items)
            {
                if (threadShouldExit())
                    return;

                result.columns[item.key] = compute (item, request.viewMin, request.viewMax, request.plotWidth);
            }

            const juce::ScopedLock sl (lock);
            completed = std::move (result);
            hasResult = true;
        }
    }

    juce::CriticalSection lock;
    Request pending;
    Result completed;
    bool hasPending = false;
    bool hasResult = false;
};

namespace
{
constexpr int smoothingOverrideDenoms[] = { -1, 0, 48, 24, 12, 6, 3, 2, 1 }; // コンボ id 1..9
constexpr float coherenceThresholds[] = { 0.5f, 0.6f, 0.7f, 0.8f, 0.9f };    // コンボ id 1..5

juce::String snapshotKey (std::uint64_t id) { return "s" + juce::String ((juce::int64) id); }
} // namespace

TransferFunctionTab::TransferFunctionTab (UiContext& context)
    : ctx (context)
{
    liveSmoothingWorker = std::make_unique<LiveSmoothingWorker>();
    liveSmoothingWorker->start();

    overlaySmoothingWorker = std::make_unique<OverlaySmoothingWorker>();
    overlaySmoothingWorker->start();

    addAndMakeVisible (graph);

    // ---- グラフのツールバー（issue #144 / #145）----
    styleCombo (viewModeCombo);
    viewModeCombo.addItem (strings::viewModeThree, 1);
    viewModeCombo.addItem (strings::viewModeTwo, 2);
    viewModeCombo.addItem (strings::viewModeAmpOnly, 3);
    viewModeCombo.setSelectedId (1, juce::dontSendNotification);
    viewModeCombo.setTooltip (strings::viewModeTooltip);
    viewModeCombo.onChange = [this]
    {
        const auto id = viewModeCombo.getSelectedId();
        graph.setViewMode (id == 2 ? ViewMode::ampPhase : id == 3 ? ViewMode::ampOnly : ViewMode::ampPhaseCoh);
        saveViewSettings();
    };
    addAndMakeVisible (viewModeCombo);

    styleCombo (scaleCombo);
    scaleCombo.addItem (strings::scaleAuto, 1);
    scaleCombo.addItem (juce::String (u8"±6 dB"), 2);
    scaleCombo.addItem (juce::String (u8"±12 dB"), 3);
    scaleCombo.addItem (juce::String (u8"±24 dB"), 4);
    scaleCombo.addItem (juce::String (u8"±48 dB"), 5);
    scaleCombo.setSelectedId (1, juce::dontSendNotification);
    scaleCombo.setTooltip (strings::scaleTooltip);
    scaleCombo.onChange = [this]
    {
        static constexpr ScalePreset presets[] = { ScalePreset::autoScale, ScalePreset::preset6dB,
                                                   ScalePreset::preset12dB, ScalePreset::preset24dB,
                                                   ScalePreset::preset48dB };
        graph.setScalePreset (presets[juce::jlimit (1, 5, scaleCombo.getSelectedId()) - 1]);
        saveViewSettings();
    };
    addAndMakeVisible (scaleCombo);

    styleToggle (blankingToggle);
    blankingToggle.setButtonText (strings::coherenceBlankingToggle);
    blankingToggle.setTooltip (strings::coherenceBlankingTooltip);
    blankingToggle.onClick = [this]
    {
        graph.setCoherenceBlanking (blankingToggle.getToggleState());
        saveViewSettings();
    };
    addAndMakeVisible (blankingToggle);

    styleCombo (coherenceThresholdCombo);

    for (int i = 0; i < (int) std::size (coherenceThresholds); ++i)
        coherenceThresholdCombo.addItem (strings::gammaSquared + " " + juce::String (coherenceThresholds[i], 1), i + 1);

    coherenceThresholdCombo.setSelectedId (4, juce::dontSendNotification);
    coherenceThresholdCombo.setTooltip (strings::coherenceThresholdTooltip);
    coherenceThresholdCombo.onChange = [this]
    {
        graph.setCoherenceThreshold (coherenceThresholds[juce::jlimit (1, 5, coherenceThresholdCombo.getSelectedId()) - 1]);
        saveViewSettings();
    };
    addAndMakeVisible (coherenceThresholdCombo);

    styleButton (peakMarkerButton);
    peakMarkerButton.setButtonText (strings::markerPeakButton);
    peakMarkerButton.setTooltip (strings::markerTooltip);
    peakMarkerButton.onClick = [this] { graph.addMarkerAtPeak(); };
    addAndMakeVisible (peakMarkerButton);

    styleButton (clearMarkersButton);
    clearMarkersButton.setButtonText (strings::markerClearButton);
    clearMarkersButton.onClick = [this] { graph.clearMarkers(); };
    addAndMakeVisible (clearMarkersButton);

    // ---- スナップショット一覧 ----
    snapshotList.setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
    snapshotList.setRowHeight (22);
    snapshotList.setMultipleSelectionEnabled (false);
    addAndMakeVisible (snapshotList);

    styleButton (saveButton);
    saveButton.setButtonText (strings::snapshotSaveButton);
    saveButton.onClick = [this] { showSaveSnapshotDialog(); };
    addAndMakeVisible (saveButton);

    styleButton (deleteButton);
    deleteButton.setButtonText (strings::snapshotDeleteButton);
    deleteButton.onClick = [this] { deleteSnapshot (snapshotList.getSelectedRow()); };
    addAndMakeVisible (deleteButton);

    // 削除の取り消し（issue #148）: 削除から 15 秒間だけ表示する
    styleAccentButton (undoButton);
    undoButton.setButtonText (strings::undoDeleteButton);
    undoButton.onClick = [this] { undoDelete(); };
    addChildComponent (undoButton);

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

    // グラフをPNGで保存（仕様 6.2）
    styleButton (exportPngButton);
    exportPngButton.setButtonText (strings::exportGraphPng);
    exportPngButton.onClick = [this] { exportGraphPng(); };
    addAndMakeVisible (exportPngButton);

    for (auto* combo : { &diffComboA, &diffComboB })
    {
        styleCombo (*combo);
        combo->onChange = [this]
        {
            auto& snapshots = ctx.project.snapshots();

            for (int i = 0; i < (int) snapshots.size(); ++i)
            {
                snapshots[(size_t) i].isSelectedA = diffComboA.getSelectedId() == i + 2;
                snapshots[(size_t) i].isSelectedB = diffComboB.getSelectedId() == i + 2;
            }
            // 鍵（差分の A/B）が変わるので次の tick で再計算される（issue #130）
        };
        addAndMakeVisible (*combo);
    }

    // ---- スナップショット編集（issue #143）----
    styleButton (visibleButton);
    visibleButton.setButtonText (strings::snapshotToggleVisible);
    visibleButton.onClick = [this]
    {
        if (auto* snapshot = selectedSnapshot())
        {
            snapshot->visible = ! snapshot->visible;
            ctx.state.snapshotVersion.fetch_add (1);
        }
    };
    addAndMakeVisible (visibleButton);

    styleButton (colorButton);
    colorButton.setButtonText (strings::snapshotColorLabel);
    colorButton.onClick = [this] { showColourPicker (snapshotList.getSelectedRow()); };
    addAndMakeVisible (colorButton);

    styleButton (editNameButton);
    editNameButton.setButtonText (strings::snapshotEditNameButton);
    editNameButton.onClick = [this] { showSnapshotEditDialog (snapshotList.getSelectedRow()); };
    addAndMakeVisible (editNameButton);

    styleButton (moveUpButton);
    moveUpButton.setButtonText (strings::snapshotMoveUpButton);
    moveUpButton.onClick = [this]
    {
        const auto selected = snapshotList.getSelectedRow();
        if (selected > 0)
            moveSnapshot (selected, selected - 1);
    };
    addAndMakeVisible (moveUpButton);

    styleButton (moveDownButton);
    moveDownButton.setButtonText (strings::snapshotMoveDownButton);
    moveDownButton.onClick = [this]
    {
        const auto selected = snapshotList.getSelectedRow();
        if (selected >= 0 && selected < (int) ctx.project.snapshots().size() - 1)
            moveSnapshot (selected, selected + 1);
    };
    addAndMakeVisible (moveDownButton);

    styleButton (averageButton);
    averageButton.setButtonText (strings::snapshotAverageButton);
    averageButton.setTooltip (strings::snapshotAverageTooltip);
    averageButton.onClick = [this] { createAverageTrace(); };
    addAndMakeVisible (averageButton);

    styleToggle (phaseToggle);
    phaseToggle.setButtonText (strings::snapshotShowPhase);
    phaseToggle.onClick = [this]
    {
        if (syncingEditors)
            return;

        if (auto* snapshot = selectedSnapshot())
        {
            snapshot->showPhase = phaseToggle.getToggleState();
            ctx.state.snapshotVersion.fetch_add (1);
        }
    };
    addAndMakeVisible (phaseToggle);

    for (auto* label : { &dbOffsetLabel, &delayOffsetLabel, &smoothingOverrideLabel })
    {
        styleLabel (*label);
        addAndMakeVisible (label);
    }

    dbOffsetLabel.setText (strings::snapshotDbOffsetLabel, juce::dontSendNotification);
    delayOffsetLabel.setText (strings::snapshotDelayOffsetMsLabel, juce::dontSendNotification);
    smoothingOverrideLabel.setText (strings::snapshotSmoothingLabel, juce::dontSendNotification);

    for (auto* editor : { &dbOffsetEditor, &delayOffsetEditor })
    {
        styleTextEditor (*editor);
        editor->setInputRestrictions (9, "0123456789.-");
        editor->setText ("0", juce::dontSendNotification);
        addAndMakeVisible (*editor);
    }

    // 空欄・"-" だけの途中入力は無視する（0 として扱わない）
    auto parseNumber = [] (const juce::String& text, double& out)
    {
        const auto trimmed = text.trim();

        if (trimmed.isEmpty() || trimmed == "-" || trimmed == "." || trimmed == "-.")
            return false;

        out = trimmed.getDoubleValue();
        return std::isfinite (out);
    };

    dbOffsetEditor.onTextChange = [this, parseNumber]
    {
        double value = 0.0;

        if (syncingEditors || ! parseNumber (dbOffsetEditor.getText(), value))
            return;

        if (auto* snapshot = selectedSnapshot())
        {
            snapshot->dbOffset = juce::jlimit (-60.0, 60.0, value);
            ctx.state.snapshotVersion.fetch_add (1);
        }
    };

    delayOffsetEditor.onTextChange = [this, parseNumber]
    {
        double ms = 0.0;

        if (syncingEditors || ! parseNumber (delayOffsetEditor.getText(), ms))
            return;

        if (auto* snapshot = selectedSnapshot())
        {
            snapshot->delayOffset = juce::jlimit (-1000.0, 1000.0, ms) * snapshot->frame.sampleRate / 1000.0;
            ctx.state.snapshotVersion.fetch_add (1);
        }
    };

    styleCombo (smoothingOverrideCombo);
    smoothingOverrideCombo.addItem (strings::snapshotSelectGlobal, 1);
    smoothingOverrideCombo.addItem (strings::noSmoothingItem, 2);

    for (int i = 2; i < (int) std::size (smoothingOverrideDenoms); ++i)
        smoothingOverrideCombo.addItem ("1/" + juce::String (smoothingOverrideDenoms[i]) + " oct", i + 1);

    smoothingOverrideCombo.setSelectedId (1, juce::dontSendNotification);
    smoothingOverrideCombo.onChange = [this]
    {
        if (syncingEditors)
            return;

        if (auto* snapshot = selectedSnapshot())
        {
            const auto id = juce::jlimit (1, (int) std::size (smoothingOverrideDenoms), smoothingOverrideCombo.getSelectedId());
            snapshot->smoothingOverride = smoothingOverrideDenoms[id - 1];
            ctx.state.snapshotVersion.fetch_add (1);
        }
    };
    addAndMakeVisible (smoothingOverrideCombo);

    hintLabel.setText (strings::noSnapshots, juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintLabel);

    refreshSnapshotList();
}

TransferFunctionTab::~TransferFunctionTab() = default;

void TransferFunctionTab::setSettings (juce::PropertiesFile* settingsToUse)
{
    settings = settingsToUse;

    if (settings == nullptr)
        return;

    viewModeCombo.setSelectedId (juce::jlimit (1, 3, settings->getIntValue ("tfViewMode", 1)), juce::sendNotificationSync);
    scaleCombo.setSelectedId (juce::jlimit (1, 5, settings->getIntValue ("tfScalePreset", 1)), juce::sendNotificationSync);
    coherenceThresholdCombo.setSelectedId (juce::jlimit (1, 5, settings->getIntValue ("tfCoherenceThreshold", 4)),
                                           juce::sendNotificationSync);
    blankingToggle.setToggleState (settings->getBoolValue ("tfCoherenceBlanking", false), juce::dontSendNotification);
    graph.setCoherenceBlanking (blankingToggle.getToggleState());
}

void TransferFunctionTab::saveViewSettings()
{
    if (settings == nullptr)
        return;

    settings->setValue ("tfViewMode", viewModeCombo.getSelectedId());
    settings->setValue ("tfScalePreset", scaleCombo.getSelectedId());
    settings->setValue ("tfCoherenceThreshold", coherenceThresholdCombo.getSelectedId());
    settings->setValue ("tfCoherenceBlanking", blankingToggle.getToggleState());
}

eeq::core::Snapshot* TransferFunctionTab::selectedSnapshot()
{
    auto& snapshots = ctx.project.snapshots();
    const auto selected = snapshotList.getSelectedRow();
    return (selected >= 0 && selected < (int) snapshots.size()) ? &snapshots[(size_t) selected] : nullptr;
}

std::shared_ptr<const eeq::dsp::SpectrumFrame> TransferFunctionTab::sharedFrameFor (const eeq::core::Snapshot& snapshot)
{
    // スナップショットのフレームは保存後に変わらない（変えるときは id を振り直す）ので、
    // id ごとに1回だけ不変コピーを作ってワーカーと共有する（issue #128）
    auto& entry = frameCache[snapshot.id];

    if (entry == nullptr)
        entry = std::make_shared<const eeq::dsp::SpectrumFrame> (snapshot.frame);

    return entry;
}

void TransferFunctionTab::timerTick()
{
    // ---- ライブ曲線 ----
    auto framePtr = ctx.latestDisplayFrame();
    if (framePtr == nullptr)
        framePtr = lastRequestedFrame;

    const auto smoothing = ctx.state.smoothingDenom;
    const auto plotWidth = graph.getPlotWidthForCache();
    const auto viewMin = graph.getViewMinForCache();
    const auto viewMax = graph.getViewMaxForCache();
    const bool applyCorrection = ctx.correction.isActive() && ctx.state.loopbackEnabled.load();
    const auto correctionGeneration = ctx.correction.getGeneration();
    const auto delaySamples = ctx.state.showRawPhase.load() ? 0.0 : ctx.state.appliedDelaySamples.load();

    if (framePtr != nullptr)
    {
        const bool settingsChanged = smoothing != requestedSmoothing
                                   || std::abs (viewMin - liveViewMin) > 1.0e-9
                                   || std::abs (viewMax - liveViewMax) > 1.0e-9
                                   || plotWidth != livePlotWidth
                                   || applyCorrection != requestedCorrectionEnabled
                                   || correctionGeneration != requestedCorrectionGeneration
                                   || std::abs (delaySamples - requestedDelaySamples) > 1.0e-9
                                   || ctx.state.micCalibration.get() != requestedMicCalibration;

        if (settingsChanged)
            ++liveGeneration;

        if (settingsChanged || framePtr != lastRequestedFrame)
        {
            LiveSmoothingWorker::Request request;
            request.generation = liveGeneration;
            request.frame = framePtr;
            request.smoothing = smoothing;
            request.viewMin = viewMin;
            request.viewMax = viewMax;
            request.plotWidth = plotWidth;
            request.appliedDelaySamples = delaySamples;
            request.applyCorrection = applyCorrection;
            request.correctionGeneration = correctionGeneration;
            request.correction = &ctx.correction;
            request.micCalibration = ctx.state.micCalibration;
            liveSmoothingWorker->submit (std::move (request));

            lastRequestedFrame = std::move (framePtr);
            requestedSmoothing = smoothing;
            requestedCorrectionEnabled = applyCorrection;
            requestedCorrectionGeneration = correctionGeneration;
            requestedDelaySamples = delaySamples;
            requestedMicCalibration = ctx.state.micCalibration.get();
            liveViewMin = viewMin;
            liveViewMax = viewMax;
            livePlotWidth = plotWidth;
        }
    }

    LiveSmoothingWorker::Result result;
    if (liveSmoothingWorker->takeResult (result) && result.generation == liveGeneration)
    {
        displayedFrame = std::move (result.frame);
        displayedPoints = std::move (result.points);
        displayedSmoothing = result.smoothing;
    }

    // 位相の遅延補正は平滑化の前にワーカーで済ませている（issue #107）ので、グラフには 0 を渡す
    if (displayedFrame != nullptr && displayedPoints != nullptr)
        graph.setLiveFrameCached (displayedPoints, displayedFrame, displayedSmoothing, 0.0, true);

    // 入力クリップ中は平均が信頼できないことをグラフに出す（issue #146）
    graph.setClipWarning (ctx.engine.getMeasurement().isMeasuring()
                          && (ctx.engine.getRefMeter().clip.load() || ctx.engine.getMeasMeter().clip.load()));

    // 表示が信用しきれない理由を、グラフの上に自分から出す（ロードマップ B-3）
    {
        juce::StringArray notices;
        auto& measurement = ctx.engine.getMeasurement();

        const auto sweepPhase = ctx.sweep != nullptr ? ctx.sweep->getPhase() : SweepSession::Phase::idle;
        const auto sweepRunning = sweepPhase == SweepSession::Phase::playing || sweepPhase == SweepSession::Phase::tail
                               || sweepPhase == SweepSession::Phase::analysing;

        if (sweepRunning)
            notices.add (strings::noticeSweepRunning);
        else if (measurement.hasHeldResult())
            notices.add (strings::noticeSweepResult);
        else if (measurement.isMeasuring())
        {
            const auto refPeak = ctx.engine.getRefMeter().peakLinear.load();

            if (refPeak < 1.0e-4f) // −80 dBFS 未満
                notices.add (strings::noticeRefSilent);
            else if (measurement.getAverages() < 4)
                notices.add (strings::noticeFewAverages);

            if (std::abs (ctx.state.appliedDelaySamples.load()) < 0.5)
                notices.add (measurement.isMtwEnabled() ? strings::noticeNoDelayMtw : strings::noticeNoDelay);
        }

        if (ctx.correction.isActive() && ctx.state.loopbackEnabled.load())
            if (const auto latest = measurement.getLatestFrame(); latest != nullptr && ! ctx.correction.matches (*latest))
                notices.add (strings::noticeCorrectionStale);

        graph.setNotices (notices);
    }

    // ---- 重ね描き ----
    if (ctx.state.snapshotVersion.load() != lastSeenSnapshotVersion
        || ctx.state.eqVersion.load() != lastSeenEqVersion)
        refreshSnapshotList();

    requestOverlaysIfNeeded();

    OverlaySmoothingWorker::Result overlayResult;
    if (overlaySmoothingWorker->takeResult (overlayResult) && overlayResult.generation == overlayGeneration)
    {
        overlayColumns = std::move (overlayResult.columns);
        pushOverlayTraces();
    }
}

void TransferFunctionTab::requestOverlaysIfNeeded()
{
    // 平滑化結果を左右する要素だけで「鍵」を作る。色・名前・dB オフセットは描画時に効くので鍵に入れない
    const auto globalSmoothing = ctx.state.smoothingDenom;
    const auto viewMin = graph.getViewMinForCache();
    const auto viewMax = graph.getViewMaxForCache();
    const auto width = graph.getPlotWidthForCache();
    auto& snapshots = ctx.project.snapshots();

    juce::String key;
    key << viewMin << "|" << viewMax << "|" << width << "|" << globalSmoothing << "|e" << (int) ctx.state.eqVersion.load();

    OverlaySmoothingWorker::Request request;
    std::map<std::uint64_t, bool> liveIds;

    for (const auto& snapshot : snapshots)
    {
        liveIds[snapshot.id] = true;

        if (! snapshot.visible)
            continue;

        OverlaySmoothingWorker::Item item;
        item.key = snapshotKey (snapshot.id);
        item.frame = sharedFrameFor (snapshot);
        item.smoothing = snapshot.smoothingOverride >= 0 ? snapshot.smoothingOverride : globalSmoothing;
        // 位相を出すときは、保存時に適用していた遅延＋表示オフセットから、フレームに織り込み済みの分を引いて回す
        item.delayCompensationSamples = snapshot.showPhase
                                            ? snapshot.frame.residualDelayFor (snapshot.delaySamples + snapshot.delayOffset)
                                            : 0.0;
        key << "|" << item.key << ":" << item.smoothing << ":" << item.delayCompensationSamples;
        request.items.push_back (std::move (item));
    }

    // 消えたスナップショットのキャッシュを捨てる
    for (auto it = frameCache.begin(); it != frameCache.end();)
        it = liveIds.count (it->first) == 0 ? frameCache.erase (it) : std::next (it);

    if (! ctx.state.targetPoints.empty())
    {
        OverlaySmoothingWorker::Item item;
        item.key = "target";
        item.points = std::make_shared<const std::vector<eeq::dsp::GridPoint>> (ctx.state.targetPoints);
        request.items.push_back (std::move (item));
        key << "|target";
    }

    if (! ctx.state.predictionPoints.empty())
    {
        OverlaySmoothingWorker::Item item;
        item.key = "prediction";
        item.points = std::make_shared<const std::vector<eeq::dsp::GridPoint>> (ctx.state.predictionPoints);
        request.items.push_back (std::move (item));
        key << "|prediction";
    }

    // 差分 A−B（仕様 5.2）
    const auto indexA = diffComboA.getSelectedId() - 2;
    const auto indexB = diffComboB.getSelectedId() - 2;

    if (indexA >= 0 && indexB >= 0 && indexA < (int) snapshots.size() && indexB < (int) snapshots.size()
        && indexA != indexB)
    {
        OverlaySmoothingWorker::Item item;
        item.key = "diff";
        item.frame = sharedFrameFor (snapshots[(size_t) indexA]);
        item.diffB = sharedFrameFor (snapshots[(size_t) indexB]);
        item.smoothing = globalSmoothing;
        request.items.push_back (std::move (item));
        key << "|diff:" << snapshotKey (snapshots[(size_t) indexA].id) << "-" << snapshotKey (snapshots[(size_t) indexB].id);
    }

    if (key == requestedOverlayKey)
        return;

    requestedOverlayKey = key;
    request.generation = ++overlayGeneration;
    request.viewMin = viewMin;
    request.viewMax = viewMax;
    request.plotWidth = width;

    if (request.items.empty())
    {
        overlayColumns.clear();
        pushOverlayTraces();
        return;
    }

    overlaySmoothingWorker->submit (std::move (request));

    // 古い結果のうち今も有効な項目はそのまま描き続ける（再計算中にちらつかせない）
    pushOverlayTraces();
}

void TransferFunctionTab::pushOverlayTraces()
{
    std::vector<FrequencyGraphView::OverlayTrace> traces;
    auto find = [this] (const juce::String& key) -> std::shared_ptr<const std::vector<FrequencyGraphView::OverlayColumn>>
    {
        const auto it = overlayColumns.find (key);
        return it != overlayColumns.end() ? it->second : nullptr;
    };

    for (const auto& snapshot : ctx.project.snapshots())
    {
        if (! snapshot.visible)
            continue;

        if (auto columns = find (snapshotKey (snapshot.id)))
        {
            FrequencyGraphView::OverlayTrace trace;
            trace.name = snapshot.name;
            trace.colour = snapshot.colour;
            trace.thickness = 1.2f;
            trace.dbOffset = snapshot.dbOffset;
            trace.showPhase = snapshot.showPhase;
            trace.sampleRate = snapshot.frame.sampleRate;
            trace.columns = std::move (columns);
            traces.push_back (std::move (trace));
        }
    }

    if (! ctx.state.targetPoints.empty())
        if (auto columns = find ("target"))
        {
            FrequencyGraphView::OverlayTrace trace;
            trace.name = strings::eqTargetLabel;
            trace.colour = theme::get (theme::ColourId::ok);
            trace.dashed = true;
            trace.dbOffset = ctx.state.targetLevelOffsetDb; // EQ案と同じ基準で重ねる（issue #109）
            trace.columns = std::move (columns);
            traces.push_back (std::move (trace));
        }

    if (! ctx.state.predictionPoints.empty())
        if (auto columns = find ("prediction"))
        {
            FrequencyGraphView::OverlayTrace trace;
            trace.name = strings::eqPredictionLegend;
            trace.colour = theme::get (theme::ColourId::warn);
            trace.thickness = 1.0f;
            trace.columns = std::move (columns);
            traces.push_back (std::move (trace));
        }

    if (diffComboA.getSelectedId() > 1 && diffComboB.getSelectedId() > 1)
        if (auto columns = find ("diff"))
        {
            FrequencyGraphView::OverlayTrace trace;
            trace.name = strings::snapshotDiffButton;
            trace.colour = theme::get (theme::ColourId::text);
            trace.thickness = 1.4f;
            trace.dashed = true;
            trace.columns = std::move (columns);
            traces.push_back (std::move (trace));
        }

    graph.setOverlayTraces (std::move (traces));
}

void TransferFunctionTab::refreshSnapshotList()
{
    lastSeenSnapshotVersion = ctx.state.snapshotVersion.load();
    lastSeenEqVersion = ctx.state.eqVersion.load();

    snapshotList.updateContent();
    snapshotList.repaint();
    updateDiffCombos();
    syncEditorsToSelection();
    hintLabel.setVisible (ctx.project.snapshots().empty());

    // 色・名前・オフセットなど描画属性の変更は再計算なしで反映する
    pushOverlayTraces();
}

void TransferFunctionTab::updateDiffCombos()
{
    auto& snapshots = ctx.project.snapshots();

    // id 1 = 差分なし（issue #140: 差分を消す選択肢）、id 2.. = スナップショット
    auto fill = [&snapshots] (juce::ComboBox& combo, bool isA)
    {
        combo.clear (juce::dontSendNotification);
        combo.addItem (strings::diffNone, 1);

        int selectId = 1;

        for (int i = 0; i < (int) snapshots.size(); ++i)
        {
            combo.addItem (snapshots[(size_t) i].name, i + 2);

            if (isA ? snapshots[(size_t) i].isSelectedA : snapshots[(size_t) i].isSelectedB)
                selectId = i + 2;
        }

        combo.setSelectedId (selectId, juce::dontSendNotification);
    };

    fill (diffComboA, true);
    fill (diffComboB, false);
}

void TransferFunctionTab::syncEditorsToSelection()
{
    const juce::ScopedValueSetter<bool> guard (syncingEditors, true);
    auto* snapshot = selectedSnapshot();
    const bool has = snapshot != nullptr;

    for (auto* c : std::initializer_list<juce::Component*> { &deleteButton, &exportCsvButton, &exportRewButton,
                                                             &visibleButton, &colorButton, &editNameButton,
                                                             &moveUpButton, &moveDownButton, &phaseToggle,
                                                             &dbOffsetEditor, &delayOffsetEditor, &smoothingOverrideCombo })
        c->setEnabled (has);

    averageButton.setEnabled (ctx.project.snapshots().size() >= 2);

    if (! has)
        return;

    if (! dbOffsetEditor.hasKeyboardFocus (true))
        dbOffsetEditor.setText (juce::String (snapshot->dbOffset, 1), juce::dontSendNotification);

    if (! delayOffsetEditor.hasKeyboardFocus (true))
        delayOffsetEditor.setText (juce::String (snapshot->frame.sampleRate > 0.0
                                                     ? snapshot->delayOffset * 1000.0 / snapshot->frame.sampleRate
                                                     : 0.0, 2),
                                   juce::dontSendNotification);

    int comboId = 1;

    for (int i = 0; i < (int) std::size (smoothingOverrideDenoms); ++i)
        if (smoothingOverrideDenoms[i] == snapshot->smoothingOverride)
            comboId = i + 1;

    smoothingOverrideCombo.setSelectedId (comboId, juce::dontSendNotification);
    phaseToggle.setToggleState (snapshot->showPhase, juce::dontSendNotification);
    visibleButton.setButtonText (snapshot->visible ? strings::snapshotHide : strings::snapshotShow);
}

void TransferFunctionTab::selectedRowsChanged (int)
{
    syncEditorsToSelection();
}

void TransferFunctionTab::deleteKeyPressed (int lastRowSelected)
{
    deleteSnapshot (lastRowSelected);
}

void TransferFunctionTab::deleteSnapshot (int index)
{
    auto& snapshots = ctx.project.snapshots();

    if (index < 0 || index >= (int) snapshots.size())
        return;

    // 確認ダイアログの代わりに「元に戻す」を出す（測定の流れを止めない: issue #148）
    lastDeleted = std::move (snapshots[(size_t) index]);
    lastDeletedIndex = index;
    snapshots.erase (snapshots.begin() + index);
    ctx.state.snapshotVersion.fetch_add (1);

    undoButton.setButtonText (strings::undoDeleteButton + " (" + lastDeleted->name + ")");
    undoButton.setVisible (true);
    startTimer (15000);

    snapshotList.selectRow (juce::jmin (index, (int) snapshots.size() - 1));
    refreshSnapshotList();
}

void TransferFunctionTab::undoDelete()
{
    stopTimer();
    undoButton.setVisible (false);

    if (! lastDeleted.has_value())
        return;

    auto& snapshots = ctx.project.snapshots();
    const auto index = juce::jlimit (0, (int) snapshots.size(), lastDeletedIndex);
    snapshots.insert (snapshots.begin() + index, std::move (*lastDeleted));
    lastDeleted.reset();
    ctx.state.snapshotVersion.fetch_add (1);
    snapshotList.selectRow (index);
    refreshSnapshotList();
}

void TransferFunctionTab::timerCallback()
{
    stopTimer();
    undoButton.setVisible (false);
    lastDeleted.reset();
}

void TransferFunctionTab::showColourPicker (int index)
{
    auto& snapshots = ctx.project.snapshots();

    if (index < 0 || index >= (int) snapshots.size())
        return;

    // ColourSelector はデスクトップに載せないと表示されないので CallOutBox に入れる
    auto selector = std::make_unique<juce::ColourSelector> (juce::ColourSelector::showColourAtTop
                                                            | juce::ColourSelector::showSliders
                                                            | juce::ColourSelector::showColourspace);
    selector->setSize (260, 280);
    selector->setCurrentColour (snapshots[(size_t) index].colour);

    struct Listener final : juce::ChangeListener
    {
        juce::Component::SafePointer<TransferFunctionTab> owner;
        std::uint64_t snapshotId = 0;

        void changeListenerCallback (juce::ChangeBroadcaster* source) override
        {
            if (owner == nullptr)
                return;

            auto* sel = dynamic_cast<juce::ColourSelector*> (source);

            for (auto& snapshot : owner->ctx.project.snapshots())
                if (snapshot.id == snapshotId && sel != nullptr)
                {
                    snapshot.colour = sel->getCurrentColour().withAlpha (1.0f);
                    owner->ctx.state.snapshotVersion.fetch_add (1);
                }
        }
    };

    struct Holder final : juce::Component
    {
        std::unique_ptr<juce::ColourSelector> selector;
        Listener listener;

        void resized() override { selector->setBounds (getLocalBounds()); }
        ~Holder() override { selector->removeChangeListener (&listener); }
    };

    auto holder = std::make_unique<Holder>();
    holder->listener.owner = this;
    holder->listener.snapshotId = snapshots[(size_t) index].id;
    holder->selector = std::move (selector);
    holder->selector->addChangeListener (&holder->listener);
    holder->addAndMakeVisible (*holder->selector);
    holder->setSize (260, 280);

    juce::CallOutBox::launchAsynchronously (std::move (holder), colorButton.getScreenBounds(), nullptr);
}

void TransferFunctionTab::createAverageTrace()
{
    // 表示中のスナップショットを平均したトレースを作る（マイク複数点の空間平均: issue #143）。
    // 振幅はパワー平均（|H|² の平均）、位相は単位ベクトルの平均、コヒーレンスは平均。
    std::vector<const eeq::core::Snapshot*> sources;

    for (const auto& snapshot : ctx.project.snapshots())
        if (snapshot.visible)
            sources.push_back (&snapshot);

    if (sources.size() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, strings::snapshotAverageButton,
                                                strings::snapshotAverageNeedTwo, strings::okButton, this, nullptr);
        return;
    }

    const auto& first = sources.front()->frame;

    for (const auto* s : sources)
        if (s->frame.fftSize != first.fftSize || std::abs (s->frame.sampleRate - first.sampleRate) > 0.5
            || s->frame.transfer.size() != first.transfer.size())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, strings::snapshotAverageButton,
                                                    strings::snapshotAverageMismatch, strings::okButton, this, nullptr);
            return;
        }

    eeq::core::Snapshot average;
    average.frame = first;
    const auto bins = first.transfer.size();
    const auto n = (double) sources.size();

    for (size_t k = 0; k < bins; ++k)
    {
        double power = 0.0, coherence = 0.0, refPower = 0.0, measPower = 0.0;
        std::complex<double> unit;

        for (const auto* s : sources)
        {
            const std::complex<double> h (s->frame.transfer[k].real(), s->frame.transfer[k].imag());
            power += std::norm (h);
            const auto magnitude = std::abs (h);

            if (magnitude > 0.0)
                unit += h / magnitude;

            coherence += s->frame.coherence[k];
            refPower += std::pow (10.0, s->frame.refPowerDb[k] / 10.0);
            measPower += std::pow (10.0, s->frame.measPowerDb[k] / 10.0);
        }

        const auto magnitude = std::sqrt (power / n);
        const auto phase = std::abs (unit) > 0.0 ? std::arg (unit) : 0.0;
        const auto h = std::polar (magnitude, phase);
        average.frame.transfer[k] = { (float) h.real(), (float) h.imag() };
        average.frame.magnitudeDb[k] = (float) (20.0 * std::log10 (std::max (magnitude, 1.0e-9)));
        average.frame.phaseDeg[k] = (float) (phase * 180.0 / juce::MathConstants<double>::pi);
        average.frame.coherence[k] = (float) (coherence / n);
        average.frame.refPowerDb[k] = (float) (10.0 * std::log10 (std::max (refPower / n, 1.0e-30)));
        average.frame.measPowerDb[k] = (float) (10.0 * std::log10 (std::max (measPower / n, 1.0e-30)));
    }

    average.name = strings::snapshotAverageName + " (" + juce::String ((int) sources.size()) + ")";
    average.systemName = sources.front()->systemName;
    average.micPoint = strings::snapshotAverageMicPoint;
    average.timestamp = juce::Time::getCurrentTime();
    average.temperatureC = sources.front()->temperatureC;
    average.correctionApplied = sources.front()->correctionApplied;
    average.note = strings::snapshotAverageNote;
    average.colour = theme::get (theme::ColourId::text);

    for (const auto* s : sources)
        average.note << "\n- " << s->name;

    auto& snapshots = ctx.project.snapshots();
    snapshots.push_back (std::move (average));
    ctx.state.snapshotVersion.fetch_add (1);
    snapshotList.selectRow ((int) snapshots.size() - 1);
    refreshSnapshotList();
}

void TransferFunctionTab::showSaveSnapshotDialog()
{
    if (dialogOpen)
        return;

    eeq::dsp::SpectrumFrame currentFrame;
    if (const auto latest = ctx.latestDisplayFrame())
        currentFrame = *latest;

    if (currentFrame.averages == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            strings::snapshotDialogTitle,
            juce::String (u8"測定結果がまだありません。測定を開始し、平均がたまってから保存してください。"),
            strings::okButton, this, nullptr);
        return;
    }

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
    auto safeWindowPtr = std::make_shared<juce::Component::SafePointer<juce::AlertWindow>> (window);
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, safeWindowPtr] (int result)
        {
            dialogOpen = false;

            if (result != 1 || safeWindowPtr == nullptr || *safeWindowPtr == nullptr)
                return;

            auto* safeWindow = safeWindowPtr->getComponent();

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

            if (const auto latest = ctx.latestDisplayFrame()) // 画面に出ているのと同じもの（MTW なら MTW）
                snapshot.frame = *latest;
            snapshot.delaySamples = ctx.state.appliedDelaySamples.load();

            if (snapshot.frame.averages == 0)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    strings::snapshotDialogTitle,
                    juce::String (u8"測定結果がなくなったため、スナップショットを保存できませんでした。"),
                    strings::okButton, this, nullptr);
                return;
            }

            // 画面と同じ補正を適用してから保存する（issue #32）:
            // 表示・EQ案・保存データの3つが同じものを指すようにする。
            // 補正済みであることも .eea に記録する
            if (ctx.correction.isActive() && ctx.state.loopbackEnabled.load()
                && ctx.correction.matches (snapshot.frame))
            {
                eeq::dsp::SpectrumFrame corrected;
                ctx.correction.apply (snapshot.frame, corrected, true);
                snapshot.frame = corrected;
                snapshot.correctionApplied = true;
            }

            // マイク補正も表示と同じようにかけて保存する
            ctx.state.applyMicCalibration (snapshot.frame);

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
            const auto error = eeq::core::exporters::writePng (image, file);

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        strings::snapshotExportTitle, error, strings::okButton,
                                                        nullptr, nullptr);
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

    // 表示チェック（クリックで切替）と色見本。表示OFFは名前も薄くする（仕様 5.5: 色だけで伝えない）
    const juce::Rectangle<float> box (5.0f, (float) height / 2 - 6.0f, 12.0f, 12.0f);
    g.setColour (snapshot.colour);

    if (snapshot.visible)
        g.fillRoundedRectangle (box, 2.0f);
    else
        g.drawRoundedRectangle (box, 2.0f, 1.5f);

    g.setColour (theme::get (theme::ColourId::text).withAlpha (snapshot.visible ? 1.0f : 0.4f));
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    auto text = snapshot.name;

    if (snapshot.systemName.isNotEmpty())
        text += "  [" + snapshot.systemName + "]";

    if (snapshot.micPoint.isNotEmpty())
        text += " @" + snapshot.micPoint;

    if (std::abs (snapshot.dbOffset) > 1.0e-6)
        text << "  " << (snapshot.dbOffset > 0 ? "+" : "") << juce::String (snapshot.dbOffset, 1) << " dB";

    if (! snapshot.visible)
        text << "  " << strings::snapshotHiddenSuffix;

    g.drawText (text, 24, 0, width - 28, height, juce::Justification::centredLeft);
}

void TransferFunctionTab::listBoxItemClicked (int rowNumber, const juce::MouseEvent& event)
{
    auto& snapshots = ctx.project.snapshots();

    if (rowNumber < 0 || rowNumber >= (int) snapshots.size())
        return;

    // 色見本（チェック）のクリックで表示ON/OFFを切り替える
    if (event.position.x < 22.0f)
    {
        snapshots[(size_t) rowNumber].visible = ! snapshots[(size_t) rowNumber].visible;
        ctx.state.snapshotVersion.fetch_add (1);
    }
}

void TransferFunctionTab::listBoxItemDoubleClicked (int rowNumber, const juce::MouseEvent& event)
{
    if (event.position.x >= 22.0f)
        showSnapshotEditDialog (rowNumber);
}

// スナップショット編集ダイアログ（名前・メモ・マイクポイント）
void TransferFunctionTab::showSnapshotEditDialog (int index)
{
    if (dialogOpen || index < 0 || index >= (int) ctx.project.snapshots().size())
        return;

    dialogOpen = true;
    const auto& snapshot = ctx.project.snapshots()[(size_t) index];
    const auto snapshotId = snapshot.id;

    auto* window = new juce::AlertWindow (strings::snapshotEditTitle, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", snapshot.name, strings::snapshotNameLabel);
    window->addTextEditor ("note", snapshot.note, strings::venueSnapshotMemo);

    juce::StringArray micPointNames { strings::notSelected };
    for (const auto& point : ctx.project.currentVenue().micPoints)
        micPointNames.add (point.name);
    window->addComboBox ("micPoint", micPointNames, strings::venueMicPoints);
    window->getComboBoxComponent ("micPoint")->setText (snapshot.micPoint.isNotEmpty() ? snapshot.micPoint
                                                                                       : strings::notSelected,
                                                        juce::dontSendNotification);

    window->addButton (strings::snapshotSaveButton, 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (strings::snapshotCancelButton, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto safeWindowPtr = std::make_shared<juce::Component::SafePointer<juce::AlertWindow>> (window);
    juce::Component::SafePointer<TransferFunctionTab> safeThis { this };
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safeThis, safeWindowPtr, snapshotId] (int result)
        {
            if (safeThis == nullptr)
                return;

            safeThis->dialogOpen = false;

            if (result != 1 || *safeWindowPtr == nullptr)
                return;

            auto* safeWindow = safeWindowPtr->getComponent();

            // 編集中に並べ替え・削除されても正しい対象に書く（index ではなく id で探す）
            for (auto& s : safeThis->ctx.project.snapshots())
            {
                if (s.id != snapshotId)
                    continue;

                s.name = safeWindow->getTextEditorContents ("name").trim();

                if (s.name.isEmpty())
                    s.name = s.timestamp.formatted ("%H:%M:%S");

                s.note = safeWindow->getTextEditorContents ("note");
                const auto mic = safeWindow->getComboBoxComponent ("micPoint")->getText();
                s.micPoint = mic == strings::notSelected ? juce::String() : mic;
                safeThis->ctx.state.snapshotVersion.fetch_add (1);
            }
        }), true);
}

void TransferFunctionTab::moveSnapshot (int fromIndex, int toIndex)
{
    auto& snapshots = ctx.project.snapshots();

    if (fromIndex < 0 || fromIndex >= (int) snapshots.size() || toIndex < 0 || toIndex >= (int) snapshots.size()
        || fromIndex == toIndex)
        return;

    auto snapshot = std::move (snapshots[(size_t) fromIndex]);
    snapshots.erase (snapshots.begin() + fromIndex);
    snapshots.insert (snapshots.begin() + toIndex, std::move (snapshot));

    ctx.state.snapshotVersion.fetch_add (1);
    snapshotList.selectRow (toIndex);
    refreshSnapshotList();
}

void TransferFunctionTab::resized()
{
    // 2026-11 リデザイン: [グラフ（上にツールバー）] / [トレース: 一覧 | 選んだトレースの調整 | 操作]
    auto bounds = getLocalBounds().reduced (16);
    cardRects.clear();

    auto tracesCard = bounds.removeFromBottom (juce::jlimit (190, 240, bounds.getHeight() / 4 + 20));
    bounds.removeFromBottom (14);
    auto graphCard = bounds;
    cardRects.push_back (graphCard);
    cardRects.push_back (tracesCard);

    // ---- グラフ ----
    auto graphArea = graphCard.reduced (12, 10);
    auto toolbar = graphArea.removeFromTop (30);
    viewModeCombo.setBounds (toolbar.removeFromLeft (170)); toolbar.removeFromLeft (8);
    scaleCombo.setBounds (toolbar.removeFromLeft (110)); toolbar.removeFromLeft (14);
    blankingToggle.setBounds (toolbar.removeFromLeft (150)); toolbar.removeFromLeft (4);
    coherenceThresholdCombo.setBounds (toolbar.removeFromLeft (90)); toolbar.removeFromLeft (10);
    clearMarkersButton.setBounds (toolbar.removeFromRight (120)); toolbar.removeFromRight (8);
    peakMarkerButton.setBounds (toolbar.removeFromRight (juce::jmin (140, juce::jmax (0, toolbar.getWidth()))));
    graphArea.removeFromTop (8);
    graph.setBounds (graphArea);

    // ---- トレース ----
    auto traces = tracesCard.reduced (14, 10);
    tracesHeaderArea = traces.removeFromTop (22);
    traces.removeFromTop (6);

    // 右: 操作（保存・書き出し）
    auto buttonColumn = traces.removeFromRight (150);
    traces.removeFromRight (12);
    auto stack = [&buttonColumn] (juce::Component& c)
    {
        c.setBounds (buttonColumn.removeFromTop (26));
        buttonColumn.removeFromTop (5);
    };
    stack (saveButton);
    stack (exportCsvButton);
    stack (exportRewButton);
    stack (exportPngButton);
    stack (deleteButton);
    undoButton.setBounds (buttonColumn.removeFromTop (26));

    // 中: 選んだトレースの調整
    auto editColumn = traces.removeFromRight (juce::jmin (380, traces.getWidth() / 2));
    traces.removeFromRight (12);
    auto editRow = [&editColumn] () { auto r = editColumn.removeFromTop (26); editColumn.removeFromTop (5); return r; };

    {
        auto r = editRow();
        const auto w = (r.getWidth() - 12) / 4;
        visibleButton.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4);
        colorButton.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4);
        editNameButton.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4);
        averageButton.setBounds (r);
    }
    {
        auto r = editRow();
        const auto w = (r.getWidth() - 8) / 3;
        moveUpButton.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4);
        moveDownButton.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4);
        phaseToggle.setBounds (r);
    }
    {
        auto r = editRow();
        dbOffsetLabel.setBounds (r.removeFromLeft (80));
        dbOffsetEditor.setBounds (r.removeFromLeft (64));
        r.removeFromLeft (10);
        delayOffsetLabel.setBounds (r.removeFromLeft (110));
        delayOffsetEditor.setBounds (r.removeFromLeft (juce::jmin (64, r.getWidth())));
    }
    {
        auto r = editRow();
        smoothingOverrideLabel.setBounds (r.removeFromLeft (80));
        smoothingOverrideCombo.setBounds (r.removeFromLeft (juce::jmin (180, r.getWidth())));
    }
    {
        auto r = editRow();
        diffLabel.setBounds (r.removeFromLeft (80));
        const auto w = (r.getWidth() - 4) / 2;
        diffComboA.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (4);
        diffComboB.setBounds (r);
    }

    // 左: 一覧
    snapshotList.setBounds (traces);
    hintLabel.setBounds (traces);
}

void TransferFunctionTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    for (const auto& card : cardRects)
    {
        g.setColour (theme::get (theme::ColourId::raised));
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (theme::get (theme::ColourId::controlOutline));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText (strings::tracesTitle, tracesHeaderArea, juce::Justification::centredLeft);
    g.setColour (theme::get (theme::ColourId::textDim));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (strings::tracesHint, tracesHeaderArea.withTrimmedLeft (110), juce::Justification::centredLeft, true);
}

} // namespace eeq::ui
