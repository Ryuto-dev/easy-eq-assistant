#pragma once

#include "Core/JuceModules.h"

#include "FrequencyGraphView.h"
#include "UiContext.h"

#include <cstdint>
#include <memory>

namespace eeq::ui
{

// 「伝達関数」タブ（仕様 5.1 上段メイン表示）。
// 振幅・位相・コヒーレンスのグラフに、スナップショット・ターゲット・EQ予測を重ねる。
// スナップショットの表示ON/OFF、差分（A−B）、CSV/REW書き出しもここ（仕様 3.5 / 5.2 / 6.2）。
// Issue #143: スナップショット管理機能（色・名前・表示順・dB/遅延オフセット・平滑化）を追加
class TransferFunctionTab : public juce::Component,
                            private juce::ListBoxModel
{
public:
    explicit TransferFunctionTab (UiContext& context);
    ~TransferFunctionTab() override;

    /** 30Hzタイマーから呼ぶ */
    void timerTick();

    /** スナップショット一覧が変わったときに呼ぶ（version で変化検出） */
    void refreshSnapshotList();

    void resized() override;

    /** スナップショット保存ダイアログを出す（キーボードのS・下部バーのボタンからも呼ぶ） */
    void showSaveSnapshotDialog();

    /** グラフをPNGで保存（仕様 6.2） */
    void exportGraphPng();

private:
    class LiveSmoothingWorker;
    class OverlaySmoothingWorker;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked (int rowNumber, const juce::MouseEvent&) override;

    void rebuildOverlays();
    void computeDiffCurve();
    void updateDiffCombos();

    // Issue #143: スナップショット編集用
    void showSnapshotEditDialog (int index);
    void moveSnapshot (int fromIndex, int toIndex);

    UiContext& ctx;
    FrequencyGraphView graph;
    juce::ListBox snapshotList { {}, this };
    juce::TextButton saveButton, deleteButton, exportCsvButton, exportRewButton, exportPngButton;
    juce::ComboBox diffComboA, diffComboB;
    juce::Label diffLabel, hintLabel;

    // Issue #143: スナップショット編集コントロール
    juce::TextButton colorButton;       // 色選択
    juce::TextButton editNameButton;    // 名前変更
    juce::TextButton moveUpButton;      // 上へ移動
    juce::TextButton moveDownButton;    // 下へ移動
    juce::Label dbOffsetLabel;          // dBオフセット
    juce::TextEditor dbOffsetEditor;    // dBオフセット入力
    juce::Label delayOffsetLabel;       // 遅延オフセット
    juce::TextEditor delayOffsetEditor; // 遅延オフセット入力
    juce::Label smoothingOverrideLabel; // 平滑化オーバーライド
    juce::ComboBox smoothingOverrideCombo; // 平滑化オーバーライド選択

    bool dialogOpen = false;
    std::vector<eeq::dsp::GridPoint> diffPoints;
    int cachedSmoothing = -1;
    int displayedSmoothing = -1;
    uint32_t lastSeenSnapshotVersion = 0;
    uint32_t lastSeenEqVersion = 0;
    juce::Rectangle<int> graphBounds;
    int editingSnapshotIndex = -1;      // 現在編集中のスナップショット

    // ---- 表示用平滑化はワーカーで計算し、UIと共有ポインタで受け渡す ----
    std::unique_ptr<LiveSmoothingWorker> liveSmoothingWorker;
    std::unique_ptr<OverlaySmoothingWorker> overlaySmoothingWorker;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> lastRequestedFrame;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> displayedFrame;
    std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> displayedPoints;
    std::uint64_t settingsGeneration = 0;
    std::uint64_t requestedCorrectionGeneration = 0;
    bool requestedCorrectionEnabled = false;
    int requestedSmoothing = -1;
    double cachedViewMin = 0.0;
    double cachedViewMax = 0.0;
    int cachedPlotWidth = 0;

    // オーバーレイ平滑化結果のキャッシュ
    std::vector<std::vector<FrequencyGraphView::OverlayColumn>> cachedOverlayColumns;
    int cachedOverlaySmoothing = -1;
    uint32_t cachedOverlayVersion = 0;
};

} // namespace eeq::ui
