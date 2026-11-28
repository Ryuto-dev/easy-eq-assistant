#pragma once

#include <vector>

#include "Core/JuceModules.h"

#include "FrequencyGraphView.h"
#include "UiContext.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>

namespace eeq::ui
{

// 「伝達関数」タブ（仕様 5.1 上段メイン表示）。
// 振幅・位相・コヒーレンスのグラフに、スナップショット・ターゲット・EQ予測・差分を重ねる。
// スナップショット（トレース）管理: 表示切替・色・名前・順序・dB/遅延オフセット・平滑化・位相表示・
// トレース平均・削除の取り消し（issue #143 / #148）。CSV/REW/PNG 書き出し（仕様 6.2）。
//
// 平滑化はすべてワーカースレッドで行い、ワーカーへは不変データ（shared_ptr<const>）だけを渡す
// （issue #128: UI 所有のデータを生ポインタで共有しない）。
class TransferFunctionTab : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Timer
{
public:
    explicit TransferFunctionTab (UiContext& context);
    ~TransferFunctionTab() override;

    /** 30Hzタイマーから呼ぶ */
    void timerTick();

    /** スナップショット一覧が変わったときに呼ぶ（version で変化検出） */
    void refreshSnapshotList();

    void resized() override;
    void paint (juce::Graphics&) override;

    /** スナップショット保存ダイアログを出す（キーボードのS・下部バーのボタンからも呼ぶ） */
    void showSaveSnapshotDialog();

    /** グラフをPNGで保存（仕様 6.2） */
    void exportGraphPng();

    /** 表示設定の保存先（表示モード・縦軸・ブランキング） */
    void setSettings (juce::PropertiesFile* settingsToUse);

    FrequencyGraphView& getGraphForTest() noexcept { return graph; }

private:
    class LiveSmoothingWorker;
    class OverlaySmoothingWorker;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked (int rowNumber, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int rowNumber, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;

    void timerCallback() override; // 取り消しボタンの表示期限

    void requestOverlaysIfNeeded();
    void pushOverlayTraces();
    void updateDiffCombos();
    void syncEditorsToSelection();
    void showSnapshotEditDialog (int index);
    void showColourPicker (int index);
    void moveSnapshot (int fromIndex, int toIndex);
    void deleteSnapshot (int index);
    void undoDelete();
    void createAverageTrace();
    void saveViewSettings();
    eeq::core::Snapshot* selectedSnapshot();
    std::shared_ptr<const eeq::dsp::SpectrumFrame> sharedFrameFor (const eeq::core::Snapshot& snapshot);

    UiContext& ctx;
    juce::PropertiesFile* settings = nullptr;
    FrequencyGraphView graph;
    juce::ListBox snapshotList { {}, this };
    juce::TextButton saveButton, deleteButton, undoButton, exportCsvButton, exportRewButton, exportPngButton;
    juce::ComboBox diffComboA, diffComboB;
    juce::Label diffLabel, hintLabel;

    // グラフ表示のツールバー（issue #144 / #145）
    juce::ComboBox viewModeCombo, scaleCombo, coherenceThresholdCombo;
    juce::ToggleButton blankingToggle;
    juce::TextButton peakMarkerButton, clearMarkersButton;

    // スナップショット編集（issue #143）
    juce::TextButton visibleButton, colorButton, editNameButton, moveUpButton, moveDownButton, averageButton;
    juce::ToggleButton phaseToggle;
    juce::Label dbOffsetLabel, delayOffsetLabel, smoothingOverrideLabel;
    juce::TextEditor dbOffsetEditor, delayOffsetEditor;
    juce::ComboBox smoothingOverrideCombo;
    bool syncingEditors = false;

    // 削除の取り消し（issue #148）
    std::optional<eeq::core::Snapshot> lastDeleted;
    int lastDeletedIndex = -1;

    bool dialogOpen = false;
    uint32_t lastSeenSnapshotVersion = 0xffffffff;
    uint32_t lastSeenEqVersion = 0xffffffff;

    // ---- ライブ曲線の平滑化（ワーカー）----
    std::unique_ptr<LiveSmoothingWorker> liveSmoothingWorker;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> lastRequestedFrame;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> displayedFrame;
    std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> displayedPoints;
    int displayedSmoothing = -1;
    std::uint64_t liveGeneration = 0;
    std::uint64_t requestedCorrectionGeneration = 0;
    bool requestedCorrectionEnabled = false;
    int requestedSmoothing = -1;
    double liveViewMin = 0.0, liveViewMax = 0.0;
    int livePlotWidth = 0;
    double requestedDelaySamples = 0.0;

    // ---- 重ね描きの平滑化（ワーカー）。鍵が変わったときだけ再計算する（issue #130）----
    std::unique_ptr<OverlaySmoothingWorker> overlaySmoothingWorker;
    juce::String requestedOverlayKey;
    std::uint64_t overlayGeneration = 0;
    std::map<juce::String, std::shared_ptr<const std::vector<FrequencyGraphView::OverlayColumn>>> overlayColumns;
    std::map<std::uint64_t, std::shared_ptr<const eeq::dsp::SpectrumFrame>> frameCache;
    std::vector<juce::Rectangle<int>> cardRects; // 2026-11 リデザイン: グラフとトレースのカード
    juce::Rectangle<int> tracesHeaderArea;
    const void* requestedMicCalibration = nullptr; // マイク補正を変えたら表示を作り直す
};

} // namespace eeq::ui
