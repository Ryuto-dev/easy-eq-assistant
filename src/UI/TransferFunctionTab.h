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

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked (int rowNumber, const juce::MouseEvent&) override;

    void rebuildOverlays();
    void computeDiffCurve();
    void updateDiffCombos();

    UiContext& ctx;
    FrequencyGraphView graph;
    juce::ListBox snapshotList { {}, this };
    juce::TextButton saveButton, deleteButton, exportCsvButton, exportRewButton, exportPngButton;
    juce::ComboBox diffComboA, diffComboB;
    juce::Label diffLabel, hintLabel;
    bool dialogOpen = false;
    std::vector<eeq::dsp::GridPoint> diffPoints;
    int cachedSmoothing = -1;
    uint32_t lastSeenSnapshotVersion = 0;
    uint32_t lastSeenEqVersion = 0;
    juce::Rectangle<int> graphBounds;

    // ---- 表示用平滑化はワーカーで計算し、UIと共有ポインタで受け渡す ----
    std::unique_ptr<LiveSmoothingWorker> liveSmoothingWorker;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> lastRequestedFrame;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> displayedFrame;
    std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> displayedPoints;
    std::uint64_t renderGeneration = 0;
    std::uint64_t requestedCorrectionGeneration = 0;
    bool requestedCorrectionEnabled = false;
    int requestedSmoothing = -1;
    double cachedViewMin = 0.0;
    double cachedViewMax = 0.0;
    int cachedPlotWidth = 0;
};

} // namespace eeq::ui
