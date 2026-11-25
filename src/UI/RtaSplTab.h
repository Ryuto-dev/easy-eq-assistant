#pragma once

#include <JuceHeader.h>

#include "UiContext.h"

#include <vector>

namespace eeq::ui
{

// 「RTA・SPL」タブ（仕様 3.4 / 3.5.1 / 3.6）。
// - RTA: 1/3・1/6・1/12 オクターブ、ピークホールド、平均時間
// - SPL: A/C/Z、Fast/Slow、Leq（1分・15分・開始から）、校正
// - 校正: 比較校正（騒音計と比較）と校正器、オフセットの保存
// - ゲイン設定アシスト: 目標SPLと入力レベルから案内
// - S/N: ノイズフロアとの差を帯域ごとに表示し、コヒーレンスと合わせてヒントを出す
class RtaSplTab : public juce::Component
{
public:
    explicit RtaSplTab (UiContext& context);

    void timerTick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applyAnalyzerConfig();
    void startNoiseFloorMeasurement();
    void stopNoiseFloorMeasurement();
    void applyCalibrationOffset();
    void updateGainAssist();
    void updateSnHint();
    void refreshFromSettings();

    UiContext& ctx;

    // ---- RTA ----
    juce::ComboBox bandsCombo, avgTimeCombo;
    juce::Label bandsTitle, avgTimeTitle;
    juce::TextButton peakHoldResetButton;
    std::vector<double> rtaCenters;
    std::vector<float> rtaLevels, rtaPeakLevels;

    // ---- SPL ----
    juce::Label splTitle, splValueLabel, leqLabel, peakLabel, uncalibratedLabel;
    juce::ComboBox weightingCombo, timeConstantCombo;

    // ---- S/N（仕様 3.5.1）----
    juce::Label snTitle, snOverallLabel, snHintLabel;
    juce::TextButton noiseFloorButton;
    bool noiseFloorMeasuring = false;
    juce::Time noiseFloorDeadline;
    std::vector<double> floorCenters;
    std::vector<float> floorLevels;

    // ---- 校正（仕様 3.4）----
    juce::Label calibrationTitle, calibrationOffsetLabel, calibrationMeterLabel, calibrationGuideLabel;
    juce::ComboBox calibrationSignalCombo;
    juce::TextEditor calibrationMeterEditor;
    juce::TextButton calibrationStartButton, calibrationApplyButton, calibrationResetButton;

    // ---- ゲインアシスト（仕様 3.6）----
    juce::Label gainTitle, gainTargetLabel, gainMessageLabel, gainMetersLabel, gainProceedLabel;
    juce::TextEditor gainTargetEditor;
    juce::TextButton gainStartButton;

    // SPL表示のキャッシュ
    double lastSplDb = -300.0;
    float splTopDb = 0.0f;   // RTA表示の上端（校正オフセットに合わせて追従）
    int rtaGraphHeight = 240;
};

} // namespace eeq::ui
