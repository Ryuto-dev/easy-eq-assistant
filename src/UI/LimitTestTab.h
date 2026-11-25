#pragma once

#include "Core/JuceModules.h"

#include "../DSP/ImpulseResponseBuilder.h"
#include "UiContext.h"

#include <vector>

namespace eeq::ui
{

// 「上限測定」タブ（仕様 3.7 ヘッドルーム・歪み測定）。
// - レベルステップ測定: ピンクノイズを刻み上げ、SPLの伸びが足りない分を圧縮量として表示
// - 歪み測定: 対数スイープのインパルス応答から高調波（Farina法）を分離してTHDを表示
// - 熱圧縮: 同一レベルを連続出力してSPL低下を時間グラフで表示
// - 提案: 圧縮が1dBに達する手前のレベルをリミッターしきい値の目安に（dBFS / dBu / V）
// - 安全設計: 出力上限・測定時間上限・自動停止・Esc・開始前の警告確認（仕様 6.3）
class LimitTestTab : public juce::Component
{
public:
    explicit LimitTestTab (UiContext& context);
    ~LimitTestTab() override;

    void timerTick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void startStepTest();
    void stopTest (bool automatic);
    void startThdTest();
    void startThermalTest();
    void updateSuggestion();
    double currentSplFast();

    // レベルステップの進行（メッセージスレッドのタイマー＝MainComponentの30Hzで進める）
    enum class Phase { idle, settling, measuring };
    Phase stepPhase = Phase::idle;
    double stepLevel = -40.0;      // 現在のステップの出力レベル (dBFS)
    std::vector<double> stepLevels;
    std::vector<double> stepSpl;
    juce::Time phaseDeadline;
    juce::Time testStartTime;

    // THD測定
    enum class ThdPhase { idle, playing, waiting };
    ThdPhase thdPhase = ThdPhase::idle;
    double thdLevel = -20.0;
    juce::Time thdDeadline;

    // 熱圧縮
    bool thermalRunning = false;
    double thermalLevel = -20.0;
    juce::Time thermalDeadline;
    std::vector<std::pair<double, double>> thermalHistory; // (秒, SPL)
    juce::Time thermalStart;

    UiContext& ctx;

    juce::Label title, safetyTitle, warnLabel, progressLabel, suggestionLabel, thdLabel, coherenceNoteLabel;
    juce::Label ceilingLabel, stepLabel, durationLabel, ampGainLabel, speakerLabel;
    juce::TextEditor ceilingEditor, stepEditor, durationEditor, ampGainEditor, speakerEditor;
    juce::ToggleButton acknowledgeToggle;
    juce::TextButton startButton, thdButton, thermalButton;

    class StepGraph : public juce::Component
    {
    public:
        std::vector<double> levels, spl;
        void paint (juce::Graphics& g) override;
    };

    class ThermalGraph : public juce::Component
    {
    public:
        std::vector<std::pair<double, double>> samples;
        void paint (juce::Graphics& g) override;
    };

    StepGraph stepGraph;
    ThermalGraph thermalGraph;

    double suggestedLimiterDbfs = 0.0;
    bool hasSuggestion = false;

    static constexpr double settleSeconds = 1.2;
    static constexpr double measureSeconds = 0.8;
    static constexpr double compressionLimitDb = 1.0; // 圧縮1 dB で自動停止（仕様 3.7）
};

} // namespace eeq::ui
