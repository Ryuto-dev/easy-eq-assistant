#include "LevelMeter.h"

#include "Theme.h"
#include "Strings.h"

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
        clipped = true;
        clipTicks = clipTicksInitial;
    }
    else if (clipTicks > 0)
    {
        --clipTicks;
        if (clipTicks == 0)
            clipped = false;
    }

    repaint();
}

void LevelMeter::clearClip()
{
    clipped = false;
    clipTicks = 0;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto clipping = clipped || clipTicks > 0;

    // ラベル
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (clipping ? eeq::ui::theme::get (eeq::ui::theme::ColourId::danger)
                           : eeq::ui::theme::get (eeq::ui::theme::ColourId::textDim));
    g.drawText (label, bounds.removeFromTop (13), juce::Justification::centredLeft);

    // バー本体
    const auto bar = bounds.removeFromTop (11).toFloat();
    g.setColour (eeq::ui::theme::get (eeq::ui::theme::ColourId::panelBackground));
    g.fillRoundedRectangle (bar, 3.0f);

    const auto mapDb = [] (float db)
    { return (float) juce::jlimit (0.0, 1.0, (double) ((db + 60.0f) / 60.0f)); };

    const auto rmsWidth = bar.getWidth() * mapDb (rmsDb);

    g.setColour (clipping ? eeq::ui::theme::get (eeq::ui::theme::ColourId::danger)
                           : eeq::ui::theme::get (eeq::ui::theme::ColourId::accent));
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (rmsWidth, 0.0f)), 3.0f);

    // 目盛り（-12, -6, -3, 0 dBFS）
    g.setColour (eeq::ui::theme::get (eeq::ui::theme::ColourId::gridText).withAlpha (0.4f));
    g.setFont (juce::Font (juce::FontOptions (8.0f)));
    for (float db : { -12.0f, -6.0f, -3.0f, 0.0f })
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (db);
        g.drawLine (x, bar.getBottom(), x, bar.getBottom() - 3.0f, 1.0f);
        g.drawText (juce::String (db, 0), (int) x - 12, (int) bar.getBottom() + 1, 24, 10, juce::Justification::centred);
    }

    // ピークマーカー
    if (peakDb > -59.0f)
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (peakDb);
        g.setColour (eeq::ui::theme::get (eeq::ui::theme::ColourId::text).withAlpha (0.9f));
        g.fillRect (x - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }

    // ピークホールド
    if (holdDb > -59.0f)
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (holdDb);
        g.setColour (eeq::ui::theme::get (eeq::ui::theme::ColourId::text).withAlpha (0.4f));
        g.fillRect (x - 0.5f, bar.getY(), 1.0f, bar.getHeight());
    }

    // 数値表示（右側）
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.setColour (eeq::ui::theme::get (eeq::ui::theme::ColourId::text).withAlpha (0.8f));
    const auto textArea = bounds.removeFromRight (80);
    g.drawText (juce::String (rmsDb, 1) + strings::dbfsUnit, textArea, juce::Justification::centredRight);

    // クリップ表示（色だけでなく文字でも伝える: 仕様 5.5）
    if (clipping)
    {
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.setColour (eeq::ui::theme::get (eeq::ui::theme::ColourId::danger));
        g.drawText (strings::inputMeterClipLabel, bar, juce::Justification::centredRight);
    }
}

} // namespace eeq::ui
