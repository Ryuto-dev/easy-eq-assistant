#include "FrequencyGraphView.h"

#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

FrequencyGraphView::FrequencyGraphView()
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    axis.limitMin = freqLimitMin;
    axis.limitMax = freqLimitMax;
    dataDirty = true;
    viewDirty = false;
}

void FrequencyGraphView::setLiveFrame (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                                       double delaySamples, bool rawPhase)
{
    const bool changed = frame.averages != current.averages
                      || frame.fftSize != current.fftSize
                      || std::abs (frame.sampleRate - current.sampleRate) > 0.5
                      || rawPhase != showRawPhase
                      || delaySamples != appliedDelaySamples;

    current = frame;
    appliedDelaySamples = delaySamples;
    showRawPhase = rawPhase;

    if (changed || smoothingOctDenom != smoothingDenom)
    {
        smoothingDenom = smoothingOctDenom;
        dataDirty = true;  // データ変更で再計算必要
        lastOverlayVersion = -1;
    }

    repaint();
}

void FrequencyGraphView::setLiveFrameCached (const std::vector<eeq::dsp::GridPoint>& points,
                                             const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                                             double delaySamples, bool rawPhase)
{
    // 軽量版: 事前計算済み points を columns へコピーするだけ（smoothToLogGrid は呼ばない）
    // dataDirty=false にする（columns は既にキャッシュから埋まっている）
    // viewDirty は維持（ズーム/パンの再描画用）

    current = frame;
    appliedDelaySamples = delaySamples;
    showRawPhase = rawPhase;

    if (smoothingOctDenom != smoothingDenom)
    {
        smoothingDenom = smoothingOctDenom;
        lastOverlayVersion = -1;
    }

    columns.resize (points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        columns[i].freq = points[i].freq;
        columns[i].h = { (float) points[i].h.real(), (float) points[i].h.imag() };
        columns[i].coherence = points[i].coherence;
    }

    dataDirty = false;  // columns はキャッシュから埋まっているので再計算不要
    // viewDirty は維持（ズーム/パンの再描画用）
    repaint();
}

void FrequencyGraphView::setOverlays (const std::vector<OverlaySource>& sources, int version, int smoothingOctDenom)
{
    overlaySources = sources;
    overlayVersion = version;
    smoothingDenom = smoothingOctDenom;
    lastOverlayVersion = -1;
    dataDirty = true;  // オーバーレイ変更もデータ再計算が必要
    repaint();
}

juce::Rectangle<float> FrequencyGraphView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (dBLabelStripWidth + 6).withTrimmedRight (10)
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

void FrequencyGraphView::rebuildLive()
{
    dataDirty = false;
    lastWidth = getWidth();
    columns.clear();

    if (current.fftSize == 0 || current.sampleRate <= 0.0 || current.averages == 0
        || current.magnitudeDb.empty() || current.transfer.size() != current.magnitudeDb.size())
        return;

    const auto effectiveMax = std::min (axis.viewMax, current.sampleRate * 0.5);
    const auto points = eeq::dsp::smoothToLogGrid (current, smoothingDenom, axis.viewMin, effectiveMax,
                                                   juce::jmax (2, (int) plotArea().getWidth()));

    columns.resize (points.size());

    for (size_t i = 0; i < points.size(); ++i)
    {
        columns[i].freq = points[i].freq;
        columns[i].h = { (float) points[i].h.real(), (float) points[i].h.imag() };
        columns[i].coherence = points[i].coherence;
    }
}

void FrequencyGraphView::rebuildOverlays()
{
    lastOverlayVersion = overlayVersion;
    overlayColumns.assign (overlaySources.size(), {});

    const auto width = juce::jmax (2, (int) plotArea().getWidth());
    const auto effectiveMax = current.sampleRate > 0 ? std::min (axis.viewMax, current.sampleRate * 0.5)
                                                     : axis.viewMax;

    for (size_t s = 0; s < overlaySources.size(); ++s)
    {
        const auto& source = overlaySources[s];

        if (source.frame != nullptr)
        {
            const auto points = eeq::dsp::smoothToLogGrid (*source.frame, smoothingDenom,
                                                           axis.viewMin, effectiveMax, width);
            auto& out = overlayColumns[s];
            out.resize (points.size());

            for (size_t i = 0; i < points.size(); ++i)
                out[i] = { points[i].freq, { (float) points[i].h.real(), (float) points[i].h.imag() } };
        }
        else if (source.points != nullptr)
        {
            // 点列（ターゲット/予測）はグリッドにそのまま乗る
            const auto& in = *source.points;
            auto& out = overlayColumns[s];
            out.clear();

            for (const auto& point : in)
            {
                if (point.freq < axis.viewMin || point.freq > effectiveMax)
                    continue;

                out.push_back ({ point.freq, { (float) point.h.real(), (float) point.h.imag() } });
            }
        }
    }
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

void FrequencyGraphView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto plot = plotArea();
    const auto amp = amplitudeArea (plot);

    // dBラベル帯の上では dB 軸をズーム、それ以外は周波数軸（仕様 5.2）
    if (wheel.deltaY != 0.0f && event.position.x < (float) dBLabelStripWidth
        && amp.getHeight() > 0.0f)
    {
        ampAuto = false;
        const auto anchor = ampTop - (event.position.y - amp.getY()) / amp.getHeight() * ampSpan;
        const auto factor = wheel.deltaY > 0 ? 0.85f : 1.18f;
        ampSpan = juce::jlimit (12.0f, 300.0f, ampSpan * factor);
        ampTop = anchor + (event.position.y - amp.getY()) / amp.getHeight() * ampSpan;
    }
    else if (wheel.deltaY != 0.0f && plot.contains (event.position))
    {
        if (axis.zoom (event.position.x, plot, wheel.deltaY))
            viewDirty = true;
    }

    repaint();
}

void FrequencyGraphView::mouseDown (const juce::MouseEvent& event)
{
    dragStart = event.position;
    dragStartAmpTop = ampTop;
}

void FrequencyGraphView::mouseDrag (const juce::MouseEvent& event)
{
    const auto plot = plotArea();

    // 横ドラッグ: 周波数のパン（仕様 5.2）
    if (std::abs (event.position.x - dragStart.x) > std::abs (event.position.y - dragStart.y))
    {
        if (axis.pan (event.position.x - dragStart.x, plot))
            viewDirty = true;
    }
    else if (! ampAuto)
    {
        // 縦ドラッグ: dB軸のパン
        const auto amp = amplitudeArea (plot);

        if (amp.getHeight() > 0.0f)
        {
            const auto shift = (event.position.y - dragStart.y) / amp.getHeight() * ampSpan;
            ampTop = dragStartAmpTop + shift;
        }
    }

    repaint();
}

void FrequencyGraphView::mouseDoubleClick (const juce::MouseEvent&)
{
    axis.resetView();
    ampAuto = true;
    viewDirty = true;
    repaint();
}

void FrequencyGraphView::paint (juce::Graphics& g)
{
    // データ変更時のみ smoothToLogGrid 再計算
    if (dataDirty || getWidth() != lastWidth)
        rebuildLive();

    // ビュー変更（ズーム/パン）時は rebuildLive() 呼ばず、既存 columns を新しい axis で描画
    // viewDirty は再描画だけで済む

    if (lastOverlayVersion != overlayVersion || (int) overlayColumns.size() != (int) overlaySources.size())
        rebuildOverlays();

    const auto bounds = getLocalBounds().toFloat();
    const auto plot = plotArea();
    const auto ampArea = amplitudeArea (plot);
    const auto lower = lowerArea (plot);
    const auto background = theme::get (theme::ColourId::graphBackground);
    const auto grid = theme::get (theme::ColourId::grid);
    const auto gridMajor = theme::get (theme::ColourId::gridMajor);
    const auto gridText = theme::get (theme::ColourId::gridText);

    g.setColour (background);
    g.fillRoundedRectangle (bounds, 6.0f);

    // ---- 周波数グリッド（上下で共有: 仕様 5.1）----
    axis.drawGrid (g, plot, grid, gridMajor, gridText);

    // ---- 振幅エリア: dBグリッド ----
    if (ampAuto && current.averages > 0 && ! columns.empty())
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

    const auto dbStep = ampSpan > 150.0f ? 20 : ampSpan > 60.0f ? 10 : 5;

    for (int db = -160; db <= 80; db += dbStep)
    {
        const auto y = mapAmpY ((float) db, ampTop, ampSpan);

        if (y < ampArea.getY() - 1 || y > ampArea.getBottom() + 1)
            continue;

        g.setColour (grid);
        g.drawLine (ampArea.getX(), y, ampArea.getRight(), y, 1.0f);

        g.setColour (gridText);
        g.drawText (juce::String (db), 4, (int) y - 7, dBLabelStripWidth - 4, 14, juce::Justification::centredRight);
    }

    // ---- 下段: 位相グリッド ----
    for (int deg = -180; deg <= 180; deg += 90)
    {
        const auto y = lower.getY() + (180.0f - (float) deg) / 360.0f * lower.getHeight();
        g.setColour (grid);
        g.drawLine (lower.getX(), y, lower.getRight(), y, 1.0f);

        g.setColour (gridText);
        g.drawText (juce::String (deg), 4, (int) y - 7, dBLabelStripWidth - 4, 14, juce::Justification::centredRight);
    }

    // コヒーレンスしきい値（点線）
    g.setColour (theme::get (theme::ColourId::text).withAlpha (0.22f));
    const auto thY = lower.getBottom() - coherenceThreshold * lower.getHeight();
    const float dashes[] = { 4.0f, 4.0f };
    g.drawDashedLine ({ lower.getX(), thY, lower.getRight(), thY }, dashes, 2, 1.0f);

    if (current.averages == 0 || columns.empty())
    {
        g.setColour (gridText);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText (strings::graphPlaceholder, getLocalBounds(), juce::Justification::centred);
        return;
    }

    // ---- カーブ描画ヘルパ（コヒーレンスがしきい値未満の区間は薄く: 仕様 3.1 / 5.5）----
    auto strokeColumnCurve = [&] (juce::Graphics& graphics, const auto& curveColumns,
                                  const juce::Rectangle<float>& area, const juce::Colour& colour,
                                  float thickness, bool brightRunsOnly, bool dashed, float alphaDim,
                                  auto&& valueOf, auto&& yOf, auto&& isBright)
    {
        juce::Path dimPath, brightPath;
        bool inDim = false, inBright = false;

        const auto count = (int) curveColumns.size();

        for (int i = 0; i < count; ++i)
        {
            const auto& column = curveColumns[(size_t) i];
            const auto value = valueOf (column);

            if (! std::isfinite (value))
            {
                inDim = inBright = false;
                continue;
            }

            const auto x = count > 1 ? area.getX() + (float) i / (float) (count - 1) * area.getWidth() : area.getX();
            const auto y = yOf (value);
            const bool bright = isBright (column);

            if (bright && brightRunsOnly)
            {
                if (! inBright)
                {
                    brightPath.startNewSubPath (x, y);
                    inBright = true;
                }
                else
                    brightPath.lineTo (x, y);

                inDim = false;
            }
            else
            {
                if (! inDim)
                {
                    dimPath.startNewSubPath (x, y);
                    inDim = true;
                }
                else
                    dimPath.lineTo (x, y);

                inBright = false;
            }
        }

        if (dashed)
        {
            // 点線は createDashedStroke で描く（破線パスをfillPathする）
            juce::Path combined = brightPath;
            combined.addPath (dimPath);
            juce::Path dashedPath;
            const float dashPattern[] = { 5.0f, 4.0f };
            juce::PathStrokeType (thickness).createDashedStroke (dashedPath, combined, dashPattern, 2);
            graphics.setColour (colour.withAlpha (alphaDim));
            graphics.fillPath (dashedPath);
            return;
        }

        graphics.setColour (colour.withAlpha (alphaDim));
        graphics.strokePath (dimPath, juce::PathStrokeType (thickness));
        graphics.setColour (colour);
        graphics.strokePath (brightPath, juce::PathStrokeType (thickness + 0.4f));
    };

    const auto ampOf = [] (const auto& column)
    { return 20.0f * std::log10 (juce::jmax (std::abs (column.h), 1.0e-9f)); };

    // 遅延補正済み位相（仕様 3.1 / 3.3）: arg(H) − 2πf·τ
    auto phaseOfLive = [this] (const DisplayColumn& column)
    {
        auto deg = std::arg (std::complex<double> (column.h.real(), column.h.imag()))
                 * 180.0 / juce::MathConstants<double>::pi;

        if (! showRawPhase && appliedDelaySamples != 0.0 && current.sampleRate > 0.0)
        {
            // Meas が Ref より遅れるとき H = e^(-j2πfτ/fs)、位相を打ち消すには +360·f·τ/fs を足す
            const auto correction = 360.0 * column.freq * appliedDelaySamples / current.sampleRate;
            deg += correction;
        }

        return (float) std::remainder ((double) deg, 360.0);
    };

    const auto ampY = [&ampArea, top = ampTop, span = ampSpan] (float db)
    { return ampArea.getY() + (top - db) / span * ampArea.getHeight(); };
    const auto phaseY = [&lower] (float deg) { return lower.getY() + (180.0f - deg) / 360.0f * lower.getHeight(); };
    const auto cohY = [&lower] (float c) { return lower.getBottom() - c * lower.getHeight(); };

    // ---- オーバーレイ（先に描いて主役を目立たせる）----
    for (size_t s = 0; s < overlayColumns.size(); ++s)
    {
        const auto& source = overlaySources[s];
        const auto& curve = overlayColumns[s];

        if (curve.empty())
            continue;

        const auto overlayAmpOf = ampOf;
        const auto overlayPhaseOf = [] (const OverlayColumn& column)
        { return (float) (std::arg (std::complex<double> (column.h.real(), column.h.imag()))
                          * 180.0 / juce::MathConstants<double>::pi); };

        strokeColumnCurve (g, curve, ampArea, source.colour, source.thickness, false, source.dashed, 1.0f,
                           overlayAmpOf, ampY, [] (const OverlayColumn&) { return true; });

        if (source.showPhase)
            strokeColumnCurve (g, curve, lower, source.colour, source.thickness, false, source.dashed, 0.8f,
                               overlayPhaseOf, phaseY, [] (const OverlayColumn&) { return true; });
    }

    // ---- コヒーレンス（下段の背景として）----
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

    g.setColour (gridText.withAlpha (0.55f));
    g.strokePath (coherencePath, juce::PathStrokeType (1.0f));

    // ---- ライブの測定カーブ ----
    const auto liveColour = theme::get (theme::ColourId::accent);
    const auto livePhaseColour = theme::get (theme::ColourId::accent2);

    strokeColumnCurve (g, columns, ampArea, liveColour, 1.2f, true, false, dimmedAlpha,
                       ampOf, ampY,
                       [] (const DisplayColumn& column) { return column.coherence >= coherenceThreshold; });

    strokeColumnCurve (g, columns, lower, livePhaseColour, 1.2f, true, false, dimmedAlpha,
                       phaseOfLive, phaseY,
                       [] (const DisplayColumn& column) { return column.coherence >= coherenceThreshold; });

    // ---- カーソル読み取り（仕様 5.2）----
    if (hoverColumn >= 0 && hoverColumn < (int) columns.size())
    {
        const auto& column = columns[(size_t) hoverColumn];
        const auto x = ampArea.getX() + (float) hoverColumn / (float) (columns.size() - 1) * ampArea.getWidth();

        g.setColour (theme::get (theme::ColourId::text).withAlpha (0.25f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

        const auto ampDb = ampOf (column);
        const auto phaseDeg = phaseOfLive (column);
        const auto readout = LogFrequencyAxis::freqText (column.freq) + "   " + juce::String (ampDb, 1) + " dB   "
                           + juce::String (phaseDeg, 1) + strings::degreeSymbol
                           + "   " + strings::gammaSquared + " " + juce::String (column.coherence, 2);

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const auto box = juce::Rectangle<float> (ampArea.getRight() - 340.0f, ampArea.getY() + 4, 340.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (theme::get (theme::ColourId::text).withAlpha (0.9f));
        g.drawText (readout, box, juce::Justification::centred);
    }
}

} // namespace eeq::ui
