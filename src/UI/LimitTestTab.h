#pragma once

#include <JuceHeader.h>

#include "UiContext.h"

namespace eeq::ui
{

// 上限測定タブ（仕様 3.7）: レベルステップ、THD、熱圧縮、リミッター提案
class LimitTestTab : public juce::Component
{
public:
    explicit LimitTestTab (UiContext& contextIn) : ctx (contextIn) {}

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
