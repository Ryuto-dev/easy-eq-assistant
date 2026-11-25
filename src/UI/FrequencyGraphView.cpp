#include "FrequencyGraphView.h"

#include "Strings.h"

#include <cmath>

namespace eeq::ui
{
namespace
{
const juce::Colour backgroundColour { 0xff1a1c20 };
const juce::Colour gridColour { 0xff2a2d33 };
const juce::Colour gridTextColour { 0xff7a8087 };

const juce::Colour ampCurveColour { 0xff3fc9d2 };   // 振幅: シアン
const juce::Colour phaseCurveColour { 0xffe0a34f }; // 位相: アンバー
const juce::Colour coherenceColour { 0xff6a7078 };  // コヒーレンス: グレー
constexpr float dimmedAlpha = 0.25f;                // コヒーレンス未満の帯域の薄さ
} // namespace

FrequencyGraphView::FrequencyGraphView()
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void FrequencyGraphView::setResult (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom)
{
    current = frame;

    if (smoothingOctDenom != smoothingDenom)
    {
        smoothingDenom = smoothingOctDenom;
        dirty = true;
    }

    repaint();
}

juce::Rectangle<float> FrequencyGraphView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (46).withTrimmedRight (10)
               .withTrimmedTop (6).withTrimmedBottom (22);
}

juce::Rectangle<float> FrequencyGraphView::amplitudeArea (const juce::Rectangle<float>& plot) const
{
    return plot.withTrimmedBottom (plot.getHeight() * 0.38f + 4.0f);
}

juce::Rectangle<float> FrequencyGraphView::lowerArea (const juce::Rectangle<float>& plot) const
{
    return plot.withTrimmedTop (plot.getHeight() * 0.62f + 4.0f);
}

float FrequencyGraphView::freqToX (double freq, const juce::Rectangle<float>& area) const
{
    const auto t = std::log (freq / freqMin) / std::log (freqMax / freqMin);
    return area.getX() + (float) juce::jlimit (0.0, 1.0, t) * area.getWidth();
}

double FrequencyGraphView::xToFreq (float x, const juce::Rectangle<float>& area) const
{
    const auto t = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth());
    return freqMin * std::pow (freqMax / freqMin, (double) t);
}

void FrequencyGraphView::mouseMove (const juce::MouseEvent& event)
{
    const auto area = plotArea();

    if (columns.empty() || ! area.contains (event.position))
    {
        hoverColumn = -1;
    }
    else
    {
        const auto t = (event.position.x - area.getX()) / area.getWidth();
        hoverColumn = juce::jlimit (0, (int) columns.size() - 1, (int) (t * (float) columns.size()));
    }

    repaint();
}

void FrequencyGraphView::mouseExit (const juce::MouseEvent&)
{
    hoverColumn = -1;
    repaint();
}

void FrequencyGraphView::rebuildDisplay()
{
    dirty = false;
    lastWidth = getWidth();
    columns.clear();

    if (current.fftSize == 0 || current.sampleRate <= 0.0 || current.averages == 0
        || current.magnitudeDb.empty() || current.transfer.size() != current.magnitudeDb.size())
        return;

    const auto nBins = (int) current.magnitudeDb.size();
    const auto effectiveMax = std::min (freqMax, current.sampleRate * 0.5);
    const auto area = plotArea();
    const auto w = juce::jmax (2, (int) area.getWidth());
    const auto binWidth = current.sampleRate / (double) current.fftSize;

    // 1/(2·denom) オクターブの半幅を持つ帯域で複素平均する（仕様 3.1 のスムージング）
    const auto factor = smoothingDenom > 0 ? std::pow (2.0, 1.0 / (2.0 * smoothingDenom)) : 1.0;

    // Ref がエネルギーを持たない bins では H1 が不定になるため、
    // ピークから80dB以上下の bins は表示から除外する（計算値は保持したまま）
    float refPeakDb = -300.0f;

    for (const auto level : current.refPowerDb)
        refPeakDb = std::max (refPeakDb, level);

    columns.resize ((size_t) w);

    for (int px = 0; px < w; ++px)
    {
        const auto f0 = freqMin * std::pow (effectiveMax / freqMin, px / (double) w);
        const auto f1 = freqMin * std::pow (effectiveMax / freqMin, (px + 1) / (double) w);
        const auto fc = std::sqrt (f0 * f1);

        const auto lo = smoothingDenom > 0 ? fc / factor : f0;
        const auto hi = smoothingDenom > 0 ? fc * factor : f1;

        auto kLo = juce::jlimit (1, nBins - 1, (int) std::ceil (lo / binWidth));
        auto kHi = juce::jlimit (1, nBins - 1, std::max (kLo, (int) std::floor (hi / binWidth)));

        std::complex<double> sumH;
        double sumCoherence = 0.0;
        int count = 0;

        for (int k = kLo; k <= kHi; ++k)
        {
            if (current.refPowerDb[(size_t) k] < refPeakDb - 80.0f)
                continue;

            sumH += std::complex<double> (current.transfer[(size_t) k].real(),
                                          current.transfer[(size_t) k].imag());
            sumCoherence += current.coherence[(size_t) k];
            ++count;
        }

        auto& column = columns[(size_t) px];
        column.freq = fc;
        column.h = count > 0 ? std::complex<float> ((float) (sumH.real() / count), (float) (sumH.imag() / count))
                             : std::complex<float> {};
        column.coherence = count > 0 ? (float) (sumCoherence / (double) count) : 0.0f;
    }
}

void FrequencyGraphView::paint (juce::Graphics& g)
{
    if (dirty || getWidth() != lastWidth)
        rebuildDisplay();

    const auto bounds = getLocalBounds().toFloat();
    const auto plot = plotArea();
    const auto ampArea = amplitudeArea (plot);
    const auto lower = lowerArea (plot);

    g.setColour (juce::Colour (backgroundColour));
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto effectiveMax = current.sampleRate > 0 ? std::min (freqMax, current.sampleRate * 0.5) : freqMax;

    // ---- 周波数グリッド（上下で共有: 仕様 5.1）----
    g.setFont (juce::Font (juce::FontOptions (10.5f)));

    for (int p = 1; p <= 4; ++p)
    {
        for (int m = 1; m <= 9; ++m)
        {
            const auto f = (double) m * std::pow (10.0, (double) p);

            if (f < freqMin || f > effectiveMax)
                continue;

            const auto x = freqToX (f, plot);
            const bool major = (m == 1) || (m == 2) || (m == 5);

            g.setColour (juce::Colour (gridColour).withAlpha (major ? 0.9f : 0.45f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

            if (m == 1 || m == 2 || m == 5)
            {
                g.setColour (juce::Colour (gridTextColour));
                const auto text = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
                g.drawText (text, (int) x - 20, (int) plot.getBottom() + 2, 40, 14,
                            juce::Justification::centred);
            }
        }
    }

    // ---- 振幅エリア: dBグリッド ----
    if (current.averages > 0 && ! columns.empty())
    {
        // 自動スケール（滑らかに追従）
        float minAmp = 1.0e9f, maxAmp = -1.0e9f;

        for (const auto& column : columns)
        {
            const auto amp = 20.0f * std::log10 (juce::jmax (std::abs (column.h), 1.0e-9f));

            if (! std::isfinite (amp))
                continue;

            minAmp = std::min (minAmp, amp);
            maxAmp = std::max (maxAmp, amp);
        }

        if (maxAmp > minAmp)
        {
            const auto targetTop = maxAmp + 10.0f;
            const auto targetSpan = juce::jlimit (60.0f, 160.0f, (maxAmp - minAmp) + 20.0f);
            ampTop += (targetTop - ampTop) * 0.15f;
            ampSpan += (targetSpan - ampSpan) * 0.15f;
        }
    }

    const auto mapAmpY = [&ampArea] (float db, float top, float span)
    { return ampArea.getY() + (top - db) / span * ampArea.getHeight(); };

    for (int db = -100; db <= 40; db += 10)
    {
        const auto y = mapAmpY ((float) db, ampTop, ampSpan);

        if (y < ampArea.getY() - 1 || y > ampArea.getBottom() + 1)
            continue;

        g.setColour (juce::Colour (gridColour));
        g.drawLine (ampArea.getX(), y, ampArea.getRight(), y, 1.0f);

        g.setColour (juce::Colour (gridTextColour));
        g.drawText (juce::String (db), 4, (int) y - 7, 38, 14, juce::Justification::centredRight);
    }

    // ---- 下段: 位相グリッド ----
    for (int deg = -180; deg <= 180; deg += 90)
    {
        const auto y = lower.getY() + (180.0f - (float) deg) / 360.0f * lower.getHeight();
        g.setColour (juce::Colour (gridColour));
        g.drawLine (lower.getX(), y, lower.getRight(), y, 1.0f);

        g.setColour (juce::Colour (gridTextColour));
        g.drawText (juce::String (deg), 4, (int) y - 7, 38, 14, juce::Justification::centredRight);
    }

    // コヒーレンスしきい値（点線）
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    const auto thY = lower.getBottom() - coherenceThreshold * lower.getHeight();
    const float dashes[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ lower.getX(), thY, lower.getRight(), thY }, dashes, 2, 1.0f);

    if (current.averages == 0 || columns.empty())
    {
        g.setColour (juce::Colour (0xff6a7078));
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText (strings::graphPlaceholder, getLocalBounds(), juce::Justification::centred);
        return;
    }

    // ---- カーブ（コヒーレンスがしきい値未満の区間は薄く: 仕様 3.1 / 5.5）----
    auto appendCurve = [this] (juce::Path& path, const juce::Rectangle<float>& area, bool bright,
                               auto&& valueOf, auto&& yOf)
    {
        bool inRun = false;

        for (size_t i = 0; i < columns.size(); ++i)
        {
            const auto& column = columns[i];
            const auto value = valueOf (column);

            if (! std::isfinite (value))
            {
                inRun = false;
                continue;
            }

            const auto isBright = column.coherence >= coherenceThreshold;

            if (isBright != bright)
            {
                inRun = false;
                continue;
            }

            const auto x = area.getX() + (float) i / (float) (columns.size() - 1) * area.getWidth();
            const auto y = yOf (value);

            if (! inRun)
            {
                path.startNewSubPath (x, y);
                inRun = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }
    };

    const auto ampOf = [] (const DisplayColumn& column)
    { return 20.0f * std::log10 (juce::jmax (std::abs (column.h), 1.0e-9f)); };
    const auto phaseOf = [] (const DisplayColumn& column)
    { return std::arg (column.h) * 180.0f / juce::MathConstants<float>::pi; };
    const auto ampY = [&ampArea, top = ampTop, span = ampSpan] (float db)
    { return ampArea.getY() + (top - db) / span * ampArea.getHeight(); };
    const auto phaseY = [&lower] (float deg) { return lower.getY() + (180.0f - deg) / 360.0f * lower.getHeight(); };
    const auto cohY = [&lower] (float c) { return lower.getBottom() - c * lower.getHeight(); };

    // コヒーレンス（下段の背景として）
    juce::Path coherencePath;

    for (size_t i = 0; i < columns.size(); ++i)
    {
        const auto x = lower.getX() + (float) i / (float) (columns.size() - 1) * lower.getWidth();
        const auto y = cohY (juce::jlimit (0.0f, 1.0f, columns[i].coherence));

        if (i == 0)
            coherencePath.startNewSubPath (x, y);
        else
            coherencePath.lineTo (x, y);
    }

    g.setColour (juce::Colour (coherenceColour).withAlpha (0.55f));
    g.strokePath (coherencePath, juce::PathStrokeType (1.0f));

    // 振幅
    juce::Path ampDim, ampBright;
    appendCurve (ampDim, ampArea, false, ampOf, ampY);
    appendCurve (ampBright, ampArea, true, ampOf, ampY);
    g.setColour (juce::Colour (ampCurveColour).withAlpha (dimmedAlpha));
    g.strokePath (ampDim, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (ampCurveColour));
    g.strokePath (ampBright, juce::PathStrokeType (1.6f));

    // 位相
    juce::Path phaseDim, phaseBright;
    appendCurve (phaseDim, lower, false, phaseOf, phaseY);
    appendCurve (phaseBright, lower, true, phaseOf, phaseY);
    g.setColour (juce::Colour (phaseCurveColour).withAlpha (dimmedAlpha));
    g.strokePath (phaseDim, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (phaseCurveColour));
    g.strokePath (phaseBright, juce::PathStrokeType (1.6f));

    // ---- カーソル読み取り（仕様 5.2）----
    if (hoverColumn >= 0 && hoverColumn < (int) columns.size())
    {
        const auto& column = columns[(size_t) hoverColumn];
        const auto x = ampArea.getX() + (float) hoverColumn / (float) (columns.size() - 1) * ampArea.getWidth();

        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

        const auto freqText = column.freq >= 1000.0
                                  ? juce::String (column.freq / 1000.0, 2) + " kHz"
                                  : juce::String (column.freq, 1) + " Hz";
        const auto ampDb = ampOf (column);
        const auto phaseDeg = phaseOf (column);
        const auto readout = freqText + "   " + juce::String (ampDb, 1) + " dB   "
                           + juce::String (phaseDeg, 1) + strings::degreeSymbol
                           + "   " + strings::gammaSquared + " " + juce::String (column.coherence, 2);

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const auto box = juce::Rectangle<float> (ampArea.getRight() - 340.0f, ampArea.getY() + 4, 340.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawText (readout, box, juce::Justification::centred);
    }
}

} // namespace eeq::ui
