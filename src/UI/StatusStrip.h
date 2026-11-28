#pragma once

#include "Core/JuceModules.h"

#include "LevelMeter.h"
#include "UiContext.h"

#include <functional>
#include <vector>

namespace eeq::ui
{

// 上部の常時ステータス帯（ロードマップ 3.2 / 2026-11 リデザイン）。
// 「いまの測定を信じてよいか」を一目で判断できる項目だけを並べ、異常なものは色で知らせる:
//   会場・系統 | 入出力（レート/バッファ）| クロック（同一 / ドリフト補正中）| 出力（ON/OFF・レベル）|
//   遅延（適用中/未設定）| 平均（n/N）| 補正（ループバック）| 校正（SPL）| Ref/Meas メーター | 本番
// 項目をクリックすると、その設定がある画面へ移動する（onNavigate）。
class StatusStrip : public juce::Component
{
public:
    enum class Target { none, setup, measure, delay, apply, venue };

    explicit StatusStrip (UiContext& context);

    /** 30Hz タイマーから呼ぶ */
    void update();

    std::function<void()> onLiveModeRequested;
    std::function<void (Target)> onNavigate;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr int preferredHeight = 52;

private:
    enum class Tone { neutral, ok, warn, danger, accent };

    struct Pill
    {
        juce::String title, value;
        Tone tone = Tone::neutral;
        Target target = Target::none;
        juce::Rectangle<int> bounds;
        int priority = 0; // 幅が足りないときは大きい順に隠す
    };

    juce::Colour toneColour (Tone tone) const;
    void layoutPills();

    UiContext& ctx;
    std::vector<Pill> pills;
    juce::String venueName, systemName;
    juce::Rectangle<int> brandArea, contextArea, pillArea;
    LevelMeter refMeter, measMeter;
    juce::TextButton liveButton;
    juce::String lastSignature;
};

} // namespace eeq::ui
