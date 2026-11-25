#include "LevelMeter.h"

namespace eeq::ui
{

LevelMeter::LevelMeter (const juce::String& labelText)
    : label (labelText)
{
    setOpaque (false);
}

void LevelMeter::setLevels (float newPeakDb, float newRmsDb, bool clippedNow)
{
    peakDb = newPeakDb;
    rmsDb = newRmsDb;

    if (peakDb >= holdDb)
    {
        holdDb = peakDb;
        holdTicks = holdTicksInitial;
    }
    else if (holdTicks > 0)
    {
        --holdTicks;
    }
    else
    {
        holdDb = juce::jmax (peakDb, holdDb - holdDecayDbPerTick);
    }

    if (clippedNow)
    {
        clipTicks = clipTicksInitial;
    }
    else if (clipTicks > 0)
    {
        --clipTicks;
    }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto clipping = clipTicks > 0;

    // ラベル
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (clipping ? juce::Colour (0xffe05a5a) : juce::Colour (0xff9aa0a8));
    g.drawText (label, bounds.removeFromTop (13), juce::Justification::centredLeft);

    // バー本体
    const auto bar = bounds.removeFromTop (11).toFloat();
    g.setColour (juce::Colour (0xff232529));
    g.fillRoundedRectangle (bar, 3.0f);

    const auto mapDb = [] (float db)
    { return (float) juce::jlimit (0.0, 1.0, (double) ((db + 60.0f) / 60.0f)); };

    const auto rmsWidth = bar.getWidth() * mapDb (rmsDb);

    g.setColour (clipping ? juce::Colour (0xffd24b4b) : juce::Colour (0xff3fc9d2));
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (rmsWidth, 0.0f)), 3.0f);

    // ピークマーカー
    if (peakDb > -59.0f)
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (peakDb);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (x - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }

    // ピークホールド
    if (holdDb > -59.0f)
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (holdDb);
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.fillRect (x - 0.5f, bar.getY(), 1.0f, bar.getHeight());
    }

    // クリップ表示（色だけでなく文字でも伝える: 仕様 5.5）
    if (clipping)
    {
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffe05a5a));
        g.drawText ("CLIP", bar, juce::Justification::centredRight);
    }
}

} // namespace eeq::ui
