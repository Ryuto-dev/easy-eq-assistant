#pragma once

#include "Core/JuceModules.h"

#include "../DSP/EQProposer.h"
#include "FrequencyGraphView.h"
#include "UiContext.h"

namespace eeq::ui
{

// 「EQ案」タブ（仕様 4章）。
// 測定カーブとターゲット曲線の差を埋めるPEQを反復法で提案し、
// 帯域一覧（種別・周波数・ゲイン・Q、ON/OFF・手動調整）、予測カーブ、CSV/テキスト書き出しを提供する。
// Issue #149: 専用グラフを置き、ツールチップと操作発見性を改善する
class EQTab : public juce::Component,
              private juce::ListBoxModel
{
public:
    explicit EQTab (UiContext& context);

    void timerTick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // ListBoxModel（帯域一覧）
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked (int rowNumber, const juce::MouseEvent&) override;

    void buildTargetPoints();
    void runProposal();
    void updatePrediction();
    void refreshBandTable();
    void selectBandForEditing (int rowNumber);
    void exportCsv();
    void exportText();
    void loadTargetCsv();
    void updateEqGraph(); // Issue #149: グラフ更新

    UiContext& ctx;

    // 入力
    juce::ComboBox targetCombo;
    juce::Label targetTitle, tiltTitle, rangeTitle, coherenceTitle, maxBandsTitle;
    juce::TextEditor tiltEditor, minFreqEditor, maxFreqEditor, coherenceEditor;
    juce::Slider maxBandsSlider;
    juce::ToggleButton valleyBoostToggle;
    juce::TextButton targetCsvButton, createButton, exportCsvButton, exportTextButton;

    // Issue #149: EQ専用グラフ
    FrequencyGraphView eqGraph;
    juce::Label levelAlignLabel;

    // 帯域一覧と手動調整
    juce::ListBox bandList { {}, this };
    juce::Label bandTitle, selectedLabel;
    juce::ComboBox bandTypeCombo;
    juce::TextEditor bandFreqEditor, bandGainEditor, bandQEditor;
    juce::ToggleButton bandEnableToggle;
    juce::Label convergenceLabel;

    eeq::dsp::EQProposalResult lastResult;
    bool hasResult = false;
    double targetDisplayOffsetDb = 0.0; // ターゲットを測定の全体レベルに合わせて表示する量（issue #109）
    std::vector<double> gridFrequencies;
    std::vector<double> measuredDb;
    std::vector<double> weight;
    // CSV読み込みしたターゲット（freq, dB）
    std::vector<std::pair<double, double>> csvTargetPoints;
};

} // namespace eeq::ui
