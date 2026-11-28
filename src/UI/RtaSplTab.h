#pragma once

#include "Core/JuceModules.h"

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

    /** 非表示タブ用の軽量ティック: 校正オフセット待ちの進行だけを行う（issue #55） */
    void timerTickBackground();
    [[nodiscard]] float floorLevelNow (size_t index) const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applyAnalyzerConfig();
    void startNoiseFloorMeasurement();
    void stopNoiseFloorMeasurement();
    void applyCalibrationOffset();
    void processCalibrationPending();
    void updateGainAssist();
    void updateSnHint();
    void refreshFromSettings();

    UiContext& ctx;

    // ---- RTA ----
    juce::ComboBox bandsCombo, avgTimeCombo;
    juce::Label bandsTitle, avgTimeTitle;
    juce::TextButton peakHoldResetButton, splResetButton;
    std::vector<double> rtaCenters, rtaNominals;
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
    double configuredSampleRate = 0.0; // 解析器に適用済みのサンプルレート（変化検出用）
    float splTopDb = 0.0f;   // RTA表示の上端（校正オフセットに合わせて追従）
    int rtaGraphHeight = 240;
    juce::Rectangle<int> rtaGraphArea;
    std::vector<juce::Rectangle<int>> cardRects; // セクションのカード（2026-11 リデザイン）

    // 校正オフセット計算用（重み付け切替後のリセット待ち）
    bool calibrationOffsetPending = false;
    double calibrationMeterValue = 0.0;
    juce::Time calibrationDeadline;
    // 比較校正は、切り替え直後の値が落ち着くのを待ってから数秒ぶんをエネルギー平均する
    // （瞬時値 1 回で決めると、ノイズの揺れ ±2 dB がそのまま校正誤差になっていた）
    juce::Time calibrationSettleUntil, calibrationCollectUntil;
    double calibrationEnergySum = 0.0;
    int calibrationSamples = 0;
};

} // namespace eeq::ui
