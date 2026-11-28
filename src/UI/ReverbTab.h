#pragma once

#include "Core/JuceModules.h"

#include "SweepSession.h"
#include "UiContext.h"

namespace eeq::ui
{

// 「残響」ページ（2026-11）。スイープを鳴らして、オクターブごとの残響時間（EDT / T20 / T30）と
// 減衰曲線を出す。同じスイープの結果は伝達関数・IR にも出る（SweepSession が保持する）。
class ReverbTab : public juce::Component
{
public:
    ReverbTab (UiContext& context, SweepSession& session);

    void timerTick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void drawTable (juce::Graphics&, juce::Rectangle<int> area);
    void drawDecay (juce::Graphics&, juce::Rectangle<int> area);

    UiContext& ctx;
    SweepSession& sweep;

    juce::Label titleLabel, hintLabel, statusLabel, lengthLabel;
    juce::ComboBox lengthCombo;
    juce::TextButton measureButton;

    std::vector<juce::Rectangle<int>> cardRects;
    juce::Rectangle<int> tableArea, decayArea, decayHeaderArea;
    std::vector<juce::Rectangle<int>> bandCells; // 表の列（クリックで減衰曲線の帯域を選ぶ）
    int selectedBand = -1;                       // -1 = 広帯域（最後の要素）
    int seenVersion = -1;
    SweepSession::Phase lastPhase = SweepSession::Phase::idle;
};

} // namespace eeq::ui
