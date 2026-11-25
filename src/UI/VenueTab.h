#pragma once

#include <JuceHeader.h>

#include "UiContext.h"

namespace eeq::ui
{

// 会場タブ（仕様 6.4）: 会場・セッション・マイクポイント・プロジェクト保存
class VenueTab : public juce::Component
{
public:
    explicit VenueTab (UiContext& contextIn) : ctx (contextIn) {}

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
