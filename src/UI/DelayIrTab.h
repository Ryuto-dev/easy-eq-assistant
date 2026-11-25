#pragma once

#include <JuceHeader.h>

#include "../DSP/ImpulseResponseBuilder.h"
#include "Strings.h"
#include "UiContext.h"

#include <memory>

namespace eeq::ui
{

// 「IR・遅延」タブ（仕様 3.3）。
// - 相互相関による遅延検出（GCC-PHAT切替・放物線補間）、遅延の適用（位相補正）
// - インパルス応答の表示（時間軸とETC。ピーク位置を0 msに揃えられる）
// - ループバック校正（システム補正。仕様 6.1）
// - 系統の管理と個別測定（仕様 3.3.4）、系統比較（ディレイタワー3.3.1 / サブの位相合わせ3.3.3 /
//   ステージ上マイク3.3.5）
class DelayIrTab : public juce::Component
{
public:
    explicit DelayIrTab (UiContext& context);
    ~DelayIrTab() override;

    void timerTick();
    void resized() override;
    void paint (juce::Graphics&) override;

    /** 下部バーの「遅延測定」から呼ぶ。相互相関で遅延を検出して表示する */
    void runDelayMeasurement();

private:
    // ---- 遅延 ----
    void updateDelayReadout();
    void applyDelay();
    void clearDelay();
    void refreshDistanceFromManualDelay();

    // ---- IR ----
    void rebuildIrIfDirty();

    // ---- ループバック校正 ----
    void measureLoopback();
    void updateLoopbackUi();

    // ---- 系統 ----
    void rebuildSystemList();
    void setExciteTarget (int index); // -1 = 全体（合成）
    void saveSystemsToVenue();

    // ---- 系統比較 ----
    void updateCompare();

    UiContext& ctx;

    // 遅延の状態
    eeq::dsp::DelayEstimate lastEstimate;
    bool hasEstimate = false;
    bool usePhat = false;
    double tempAtDelayApply = 999.0; // 遅延適用時の気温（温度ずれ予測の基準。仕様 3.8）
    juce::String delayUnit = strings::delayMsUnit; // ms / samples / m / ft（仕様 3.3.1）

    // IRのキャッシュ
    std::unique_ptr<eeq::dsp::ImpulseResponse> cachedIr;
    int cachedIrAverages = -1;
    int cachedIrWidth = -1;
    int cachedIrFftSize = 0;
    double cachedIrSampleRate = 0.0;
    bool peakAligned = true;
    bool showEtc = true;
    juce::Range<float> irTimeRangeMs { -20.0f, 100.0f };

    // 系統の選択（-1 = 全体）
    int exciteTarget = -1;
    int compareVersionSeen = -1;
    int irGraphHeight = 180;

    // ---- 遅延パネル ----
    juce::Label delayTitle, delayValueLabel, delayProposalLabel, distanceCheckLabel, temperatureNote;
    juce::TextButton measureButton, applyButton, clearButton;
    juce::ToggleButton phatToggle, rawPhaseToggle, peakAlignToggle, etcToggle;

    // 手動入力（ms と m が連動: 仕様 3.3.2）
    juce::Label manualDelayTitle, manualDistanceTitle;
    juce::TextEditor manualDelayEditor, manualDistanceEditor;
    juce::Label manualWarnLabel;

    // ---- IR表示 ----
    juce::Label irTitle;

    // ---- ループバック校正 ----
    juce::Label loopbackTitle, loopbackStatusLabel;
    juce::TextButton loopbackMeasureButton, loopbackClearButton;
    juce::ToggleButton loopbackEnableToggle;

    // ---- 系統管理 ----
    juce::Label systemsTitle, systemsHint;
    juce::TextButton systemsAddButton, systemsRemoveButton, systemsAllButton;
    juce::ListBox systemList { {}, nullptr };

    // ---- 系統比較 ----
    juce::Label compareTitle, compareARowLabel, compareBRowLabel;
    juce::ComboBox compareComboA, compareComboB;
    juce::Label compareResultLabel, compareTowerLabel, comparePolarityLabel, compareCrossoverLabel;
    juce::Label compareFineLabel;
    juce::Slider compareFineSlider;

    class SystemListModel;
    std::unique_ptr<SystemListModel> systemListModel;
};

} // namespace eeq::ui
