#pragma once

#include <JuceHeader.h>

#include "UiContext.h"

namespace eeq::ui
{

// RTA・SPLタブ（仕様 3.4）: RTA、SPL、校正、ゲインアシスト、S/N
class RtaSplTab : public juce::Component
{
public:
    explicit RtaSplTab (UiContext& contextIn) : ctx (contextIn) {}

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
