#include "LevelMeter.h"

#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

LevelMeter::LevelMeter (const juce::String& labelText)
    : label (labelText)
{
    setOpaque (false);
    setTooltip (strings::meterTooltip);
}

void LevelMeter::setLevels (float newPeakDb, float newRmsDb, bool clipLatched)
{
    const auto previousPeak = peakDb, previousRms = rmsDb, previousHold = holdDb;
    const auto previousClip = clipping;

    peakDb = newPeakDb;
    rmsDb = newRmsDb;
    clipping = clipLatched;

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

    // 変化がなければ描き直さない（無音時の無駄な再描画を避ける）
    if (std::abs (previousPeak - peakDb) > 0.05f || std::abs (previousRms - rmsDb) > 0.05f
        || std::abs (previousHold - holdDb) > 0.05f || previousClip != clipping)
        repaint();
}

void LevelMeter::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasDraggedSinceMouseDown() && onClick)
        onClick();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto danger = theme::get (theme::ColourId::danger);

    // 上段: ラベルと数値（ピークホールド値。-60 未満は "−∞" 扱い）
    auto header = bounds.removeFromTop (14);
    g.setFont (juce::Font (juce::FontOptions (11.5f, clipping ? juce::Font::bold : juce::Font::plain)));
    g.setColour (clipping ? danger : theme::get (theme::ColourId::textDim));
    g.drawText (label, header, juce::Justification::centredLeft);

    const auto valueText = clipping ? strings::inputMeterClipLabel
                                    : (holdDb > -99.0f ? juce::String (holdDb, 1) : juce::String (u8"-∞"));
    g.setColour (clipping ? danger : theme::get (theme::ColourId::text));
    g.drawText (valueText, header, juce::Justification::centredRight);

    // 下段: バー
    const auto bar = bounds.removeFromTop (10).toFloat();
    g.setColour (theme::get (theme::ColourId::control));
    g.fillRoundedRectangle (bar, 3.0f);

    const auto mapDb = [] (float db)
    { return (float) juce::jlimit (0.0, 1.0, (double) ((db + 60.0f) / 60.0f)); };

    // RMS バー: −12 dBFS 以上は注意色、クリップ時は危険色
    const auto rmsWidth = bar.getWidth() * mapDb (rmsDb);
    g.setColour (clipping ? danger
                          : peakDb > -6.0f ? theme::get (theme::ColourId::warn)
                                           : theme::get (theme::ColourId::accent));
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (rmsWidth, 0.0f)), 3.0f);

    // 目盛り（−24 / −12 / −6 / 0 dBFS の刻み）
    g.setColour (theme::get (theme::ColourId::gridText).withAlpha (0.6f));

    for (const auto db : { -24.0f, -12.0f, -6.0f, 0.0f })
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (db);
        g.fillRect (x - 0.5f, bar.getBottom() - 3.0f, 1.0f, 3.0f);
    }

    // ピークマーカーとホールド
    if (peakDb > -59.0f)
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (peakDb);
        g.setColour (theme::get (theme::ColourId::text).withAlpha (0.9f));
        g.fillRect (x - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }

    if (holdDb > -59.0f)
    {
        const auto x = bar.getX() + bar.getWidth() * mapDb (holdDb);
        g.setColour (theme::get (theme::ColourId::text).withAlpha (0.45f));
        g.fillRect (x - 0.5f, bar.getY(), 1.0f, bar.getHeight());
    }

    if (clipping)
    {
        g.setColour (danger);
        g.drawRoundedRectangle (bar, 3.0f, 1.5f);
    }
}

} // namespace eeq::ui
