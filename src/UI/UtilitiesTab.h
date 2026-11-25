#pragma once

#include <JuceHeader.h>

#include "UiContext.h"

namespace eeq::ui
{

// ツールタブ（仕様 5.6）: 8つの計算ツール
class UtilitiesTab : public juce::Component
{
public:
    explicit UtilitiesTab (UiContext& contextIn) : ctx (contextIn) {}

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
