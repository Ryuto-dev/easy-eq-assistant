#include "ReverbTab.h"

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

namespace
{
    juce::String bandName (double hz)
    {
        if (hz <= 0.0)
            return strings::reverbBroadband;
        return hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (juce::roundToInt (hz));
    }

    juce::String seconds (double value)
    {
        return value > 0.0 ? juce::String (value, 2) : juce::String (u8"—");
    }
}

ReverbTab::ReverbTab (UiContext& context, SweepSession& session) : ctx (context), sweep (session)
{
    titleLabel.setText (strings::reverbTitle, juce::dontSendNotification);
    styleLabel (titleLabel, 16.0f, false);
    addAndMakeVisible (titleLabel);

    hintLabel.setText (strings::reverbHint, juce::dontSendNotification);
    styleLabel (hintLabel, 13.0f, true);
    hintLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hintLabel);

    lengthLabel.setText (strings::reverbSweepLength, juce::dontSendNotification);
    styleLabel (lengthLabel, 13.0f, true);
    addAndMakeVisible (lengthLabel);

    lengthCombo.addItem (u8"5 秒", 5);
    lengthCombo.addItem (u8"10 秒", 10);
    lengthCombo.addItem (u8"20 秒", 20);
    lengthCombo.setSelectedId (10, juce::dontSendNotification);
    styleCombo (lengthCombo);
    addAndMakeVisible (lengthCombo);

    styleButton (measureButton, {}, "primary");
    measureButton.setButtonText (strings::reverbMeasureButton);
    measureButton.onClick = [this]
    {
        if (ctx.engine.isExcitationAudible())
            ctx.engine.stopExcitation();
        else
            sweep.startSweep ((double) lengthCombo.getSelectedId());
    };
    addAndMakeVisible (measureButton);

    statusLabel.setText (strings::reverbIdle, juce::dontSendNotification);
    styleLabel (statusLabel, 13.0f, false);
    addAndMakeVisible (statusLabel);
}

void ReverbTab::timerTick()
{
    using P = SweepSession::Phase;
    const auto phase = sweep.getPhase();

    juce::String status;
    switch (phase)
    {
        case P::playing:   status = strings::reverbPlaying + juce::String (juce::roundToInt (sweep.getProgress() * 100.0)) + "%"; break;
        case P::tail:      status = strings::reverbTail; break;
        case P::analysing: status = strings::reverbAnalysing; break;
        case P::failed:    status = strings::reverbFailed + sweep.getFailureText(); break;
        case P::done:      status = strings::reverbDone + sweep.getResultTime().formatted ("%H:%M:%S"); break;
        case P::idle:
        default:           status = sweep.getReverb() != nullptr ? strings::reverbDone + sweep.getResultTime().formatted ("%H:%M:%S")
                                                                 : strings::reverbIdle; break;
    }

    if (statusLabel.getText() != status)
        statusLabel.setText (status, juce::dontSendNotification);

    const auto busy = phase == P::playing || phase == P::tail || phase == P::analysing;
    measureButton.setButtonText (ctx.engine.isExcitationAudible() ? strings::reverbStopButton : strings::reverbMeasureButton);
    measureButton.setEnabled (! (busy && ! ctx.engine.isExcitationAudible()));

    if (sweep.getResultVersion() != seenVersion || phase != lastPhase)
    {
        seenVersion = sweep.getResultVersion();
        lastPhase = phase;
        repaint();
    }
    else if (busy)
    {
        repaint (tableArea.getUnion (decayArea));
    }
}

void ReverbTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    for (const auto& card : cardRects)
    {
        g.setColour (theme::get (theme::ColourId::raised));
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (theme::get (theme::ColourId::controlOutline));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    // 進み具合のバー
    if (const auto phase = sweep.getPhase();
        phase == SweepSession::Phase::playing || phase == SweepSession::Phase::tail || phase == SweepSession::Phase::analysing)
    {
        auto bar = statusLabel.getBounds().withY (statusLabel.getBottom() + 2).withHeight (4).toFloat();
        g.setColour (theme::get (theme::ColourId::control));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (theme::get (theme::ColourId::accent));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) sweep.getProgress()), 2.0f);
    }

    drawTable (g, tableArea);
    drawDecay (g, decayArea);
}

void ReverbTab::drawTable (juce::Graphics& g, juce::Rectangle<int> area)
{
    bandCells.clear();
    const auto dim = theme::get (theme::ColourId::textDim);
    const auto text = theme::get (theme::ColourId::text);
    const auto accent = theme::get (theme::ColourId::accent);
    const auto reverb = sweep.getReverb();

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (strings::reverbTableTitle, area.removeFromTop (26), juce::Justification::centredLeft);

    if (reverb == nullptr || reverb->bands.empty())
    {
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText (strings::reverbNoResult, area, juce::Justification::centred);
        return;
    }

    const auto& bands = reverb->bands;
    const auto rowLabelWidth = 110;
    const auto columns = (int) bands.size();
    const auto cellWidth = (area.getWidth() - rowLabelWidth) / juce::jmax (1, columns);

    // 棒グラフ（T20、無ければ EDT）。上半分
    auto chart = area.removeFromTop (area.getHeight() * 55 / 100);
    double maxRt = 0.2;
    for (const auto& b : bands)
        maxRt = std::max ({ maxRt, b.t20, b.t30, b.edt });
    // 目盛りは 4〜6 本になる「きりのいい」間隔
    double tickStep = 0.1;
    for (const auto candidate : { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0 })
        if (maxRt / candidate <= 6.0) { tickStep = candidate; break; }
    maxRt = std::ceil (maxRt / tickStep - 1.0e-9) * tickStep;

    auto plot = chart.withTrimmedLeft (rowLabelWidth).reduced (0, 6);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (double v = tickStep; v <= maxRt + 1.0e-6; v += tickStep)
    {
        const auto y = plot.getBottom() - (float) (v / maxRt) * plot.getHeight();
        g.setColour (theme::get (theme::ColourId::grid));
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (dim);
        g.drawText (juce::String (v, 1) + " s", chart.getX(), (int) y - 8, rowLabelWidth - 8, 16, juce::Justification::centredRight);
    }

    for (int i = 0; i < columns; ++i)
    {
        const auto& b = bands[(size_t) i];
        const auto value = b.t20 > 0.0 ? b.t20 : (b.t30 > 0.0 ? b.t30 : b.edt);
        auto column = juce::Rectangle<int> (plot.getX() + i * cellWidth, plot.getY(), cellWidth, plot.getHeight());
        const auto selected = (selectedBand < 0 ? columns - 1 : selectedBand) == i;

        if (value > 0.0)
        {
            const auto h = (float) (value / maxRt) * plot.getHeight();
            auto bar = column.toFloat().reduced ((float) cellWidth * 0.22f, 0.0f).withTop (plot.getBottom() - h);
            g.setColour (b.t20 > 0.0 ? accent.withAlpha (selected ? 1.0f : 0.7f) : dim.withAlpha (0.6f));
            g.fillRoundedRectangle (bar, 3.0f);
        }
    }

    // 表（帯域 / EDT / T20 / T30 / S/N）
    area.removeFromTop (6);
    const auto rowHeight = juce::jmin (26, area.getHeight() / 5);
    const juce::String rowNames[] = { strings::reverbBand, "EDT", "T20", "T30", strings::reverbDynamicRange };

    for (int row = 0; row < 5; ++row)
    {
        auto line = area.removeFromTop (rowHeight);
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (13.0f, row == 0 ? juce::Font::bold : juce::Font::plain)));
        g.drawText (rowNames[row], line.removeFromLeft (rowLabelWidth), juce::Justification::centredLeft);

        for (int i = 0; i < columns; ++i)
        {
            const auto& b = bands[(size_t) i];
            auto cell = line.removeFromLeft (cellWidth);
            const auto selected = (selectedBand < 0 ? columns - 1 : selectedBand) == i;
            juce::String value;
            auto colour = text;

            switch (row)
            {
                case 0: value = bandName (b.centreHz); colour = selected ? accent : dim; break;
                case 1: value = seconds (b.edt); break;
                case 2: value = seconds (b.t20); break;
                case 3: value = seconds (b.t30); break;
                case 4:
                    value = juce::String (juce::roundToInt (b.dynamicRangeDb)) + " dB";
                    colour = b.dynamicRangeDb >= 45.0 ? dim : (b.dynamicRangeDb >= 35.0 ? theme::get (theme::ColourId::warn)
                                                                                     : theme::get (theme::ColourId::danger));
                    break;
                default: break;
            }

            g.setColour (colour);
            g.drawText (value, cell, juce::Justification::centred);

            if (row == 0)
                bandCells.push_back (juce::Rectangle<int> (cell.getX(), chart.getY(), cell.getWidth(), line.getBottom() + rowHeight * 4 - chart.getY()));
        }
    }

    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawFittedText (strings::reverbTableNote, area.withTrimmedTop (4), juce::Justification::topLeft, 3);
}

void ReverbTab::drawDecay (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto dim = theme::get (theme::ColourId::textDim);
    const auto text = theme::get (theme::ColourId::text);
    const auto reverb = sweep.getReverb();

    auto header = area.removeFromTop (26);
    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    juce::String title = strings::reverbDecayTitle;
    if (reverb != nullptr && ! reverb->bands.empty())
    {
        const auto index = selectedBand < 0 ? (int) reverb->bands.size() - 1 : juce::jmin (selectedBand, (int) reverb->bands.size() - 1);
        const auto hz = reverb->bands[(size_t) index].centreHz;
        title << juce::String (u8"（") << (hz > 0.0 ? bandName (hz) + " Hz" : bandName (hz)) << juce::String (u8"）");
    }
    g.drawText (title, header, juce::Justification::centredLeft);

    auto plot = area.reduced (0, 4).withTrimmedLeft (44).withTrimmedBottom (20);
    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (plot.toFloat(), 6.0f);

    if (reverb == nullptr || reverb->bands.empty())
        return;

    const auto index = selectedBand < 0 ? (int) reverb->bands.size() - 1 : juce::jmin (selectedBand, (int) reverb->bands.size() - 1);
    const auto& band = reverb->bands[(size_t) index];
    const auto step = reverb->stepSeconds;

    // 時間軸: 減衰曲線の長さに合わせる（最低 0.5 秒）
    const auto span = std::max (0.5, std::max (band.envelopeDb.size(), band.edcDb.size()) * step);
    const auto minDb = -80.0;
    auto toX = [&] (double t) { return (float) plot.getX() + (float) (t / span) * (float) plot.getWidth(); };
    auto toY = [&] (double db) { return (float) plot.getY() + (float) (juce::jlimit (minDb, 0.0, db) / minDb) * (float) plot.getHeight(); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (int db = 0; db >= (int) minDb; db -= 20)
    {
        g.setColour (theme::get (theme::ColourId::grid));
        g.drawHorizontalLine ((int) toY (db), (float) plot.getX(), (float) plot.getRight());
        g.setColour (dim);
        g.drawText (juce::String (db), area.getX(), (int) toY (db) - 8, 40, 16, juce::Justification::centredRight);
    }

    const auto tick = span > 3.0 ? 1.0 : (span > 1.2 ? 0.5 : 0.1);
    for (double t = 0.0; t <= span + 1.0e-9; t += tick)
    {
        g.setColour (theme::get (theme::ColourId::grid));
        g.drawVerticalLine ((int) toX (t), (float) plot.getY(), (float) plot.getBottom());
        g.setColour (dim);
        g.drawText (juce::String (t, tick < 0.5 ? 1 : 1) + " s", (int) toX (t) - 30, plot.getBottom() + 2, 60, 16, juce::Justification::centred);
    }

    // IR の包絡（薄く）と減衰曲線（主役）
    auto drawCurve = [&] (const std::vector<float>& values, juce::Colour colour, float thickness)
    {
        juce::Path path;
        for (size_t i = 0; i < values.size(); ++i)
        {
            const auto x = toX ((double) i * step), y = toY (values[i]);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    };

    g.saveState();
    g.reduceClipRegion (plot);
    drawCurve (band.envelopeDb, dim.withAlpha (0.55f), 1.0f);
    drawCurve (band.edcDb, theme::get (theme::ColourId::accent), 2.0f);

    // T20 の当てはめ範囲（−5 / −25 dB）
    g.setColour (theme::get (theme::ColourId::warn).withAlpha (0.5f));
    for (const auto db : { -5.0, -25.0, -35.0 })
    {
        const auto y = toY (db);
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
    }
    g.restoreState();

    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (strings::reverbDecayLegend, plot.reduced (8, 6), juce::Justification::topRight);
}

void ReverbTab::mouseDown (const juce::MouseEvent& event)
{
    const auto reverb = sweep.getReverb();
    if (reverb == nullptr)
        return;

    for (int i = 0; i < (int) bandCells.size(); ++i)
        if (bandCells[(size_t) i].contains (event.getPosition()))
        {
            selectedBand = i == (int) reverb->bands.size() - 1 ? -1 : i;
            repaint();
            return;
        }
}

void ReverbTab::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    cardRects.clear();

    // 上: 測り方と操作
    auto top = bounds.removeFromTop (128);
    cardRects.push_back (top);
    bounds.removeFromTop (14);
    auto inner = top.reduced (16, 12);
    auto controls = inner.removeFromRight (juce::jmin (360, inner.getWidth() / 2));
    titleLabel.setBounds (inner.removeFromTop (26));
    hintLabel.setBounds (inner);

    auto row = controls.removeFromTop (36);
    measureButton.setBounds (row.removeFromRight (180));
    row.removeFromRight (8);
    lengthCombo.setBounds (row.removeFromRight (90).withSizeKeepingCentre (90, 30));
    lengthLabel.setBounds (row);
    controls.removeFromTop (10);
    statusLabel.setBounds (controls.removeFromTop (24));

    // 下: 左に表、右に減衰曲線（狭いときは縦に並べる）
    const auto narrow = bounds.getWidth() < 1000;
    auto tableCard = narrow ? bounds.removeFromTop (bounds.getHeight() / 2 - 7) : bounds.removeFromLeft (bounds.getWidth() * 55 / 100);
    (narrow ? bounds.removeFromTop (14) : bounds.removeFromLeft (14));
    auto decayCard = bounds;
    cardRects.push_back (tableCard);
    cardRects.push_back (decayCard);
    tableArea = tableCard.reduced (16, 12);
    decayArea = decayCard.reduced (16, 12);
}

} // namespace eeq::ui
