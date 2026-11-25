#pragma once

#include "Core/JuceModules.h"

namespace eeq::ui
{

// 横型のレベルメーター。RMSバー＋ピークマーカー＋ピークホールド＋クリップ表示。
// ボールディクス（ピークホールドの落下）はUI側で処理する（仕様 3.5 / 5.1）。
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& labelText);

    /** 30Hz程度のタイマーから呼ぶ。dB値は −100..0 を想定 */
    void setLevels (float peakDb, float rmsDb, bool clippedNow);

    void paint (juce::Graphics&) override;

private:
    juce::String label;

    float peakDb = -100.0f;
    float rmsDb = -100.0f;
    float holdDb = -100.0f;
    int holdTicks = 0;
    int clipTicks = 0;

    static constexpr int holdTicksInitial = 30; // 1秒ホールド（30Hz想定）
    static constexpr int clipTicksInitial = 45; // クリップ表示は1.5秒残す
    static constexpr float holdDecayDbPerTick = 0.5f; // 約15 dB/sで落下
};

} // namespace eeq::ui
