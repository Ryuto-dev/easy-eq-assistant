#pragma once

#include "Core/JuceModules.h"

#include <functional>

namespace eeq::ui
{

// 横型のレベルメーター（issue #146）。
// 上段: チャンネル名とピーク値（dBFS の数値）、下段: RMSバー＋ピークマーカー＋ピークホールド＋目盛り。
// クリップはエンジン側でラッチされ、メーターのクリック（または R）で解除する。
// ボールディクス（ピークホールドの落下）はUI側で処理する（仕様 3.5 / 5.1）。
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelMeter (const juce::String& labelText);

    /** 30Hz程度のタイマーから呼ぶ。dB値は −100..0 を想定。clipLatched はエンジンのクリップラッチ */
    void setLevels (float peakDb, float rmsDb, bool clipLatched);

    /** クリックされたとき（クリップ解除に使う） */
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String label;

    float peakDb = -100.0f;
    float rmsDb = -100.0f;
    float holdDb = -100.0f;
    float displayedHoldDb = 1000.0f;
    int holdTicks = 0;
    bool clipping = false;

    static constexpr int holdTicksInitial = 30; // 1秒ホールド（30Hz想定）
    static constexpr float holdDecayDbPerTick = 0.5f; // 約15 dB/sで落下
};

} // namespace eeq::ui
