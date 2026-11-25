#pragma once

#include <JuceHeader.h>

#include "UiContext.h"

namespace eeq::ui
{

// EQ案タブ（仕様 4章）: ターゲット、反復アルゴリズム、帯域一覧、予測カーブ、書き出し
class EQTab : public juce::Component
{
public:
    explicit EQTab (UiContext& contextIn) : ctx (contextIn) {}

    /** 30Hzタイマーから呼ぶ（必要なタブだけ上書きする） */
    virtual void timerTick() {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::get (theme::ColourId::panelBackground));
    }

protected:
    UiContext& ctx;
};

} // namespace eeq::ui
