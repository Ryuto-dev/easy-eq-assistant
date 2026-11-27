#include "FrequencyGraphView.h"

#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

namespace
{
juce::Colour markerColour (int index)
{
    static const juce::uint32 colours[] = { 0xffff5d5d, 0xff5dd36b, 0xff4fc3f7, 0xffffd54f,
                                            0xffce93d8, 0xffffa040, 0xffb0bec5, 0xff80deea };
    return juce::Colour (colours[index % 8]);
}

float ampDbOf (std::complex<float> h)
{
    return 20.0f * std::log10 (juce::jmax (std::abs (h), 1.0e-9f));
}

float phaseDegOf (std::complex<float> h, double freq, double delaySamples, double sampleRate)
{
    auto deg = std::arg (std::complex<double> (h.real(), h.imag())) * 180.0 / juce::MathConstants<double>::pi;

    // Meas が Ref より τ 遅れると H = e^(-j2πfτ/fs)。打ち消すには +360·f·τ/fs を足す
    if (std::abs (delaySamples) > 1.0e-9 && sampleRate > 0.0)
        deg += 360.0 * freq * delaySamples / sampleRate;

    return (float) std::remainder (deg, 360.0);
}

// 読み取り枠などの半透明背景。テーマに追従させる（issue #132: ライトテーマで黒地に黒文字にしない）
juce::Colour overlayBoxColour (float alpha)
{
    return theme::get (theme::ColourId::panelBackground).withAlpha (alpha);
}
} // namespace

FrequencyGraphView::FrequencyGraphView()
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    axis.limitMin = freqLimitMin;
    axis.limitMax = freqLimitMax;
}

bool FrequencyGraphView::hasAnythingToDraw() const noexcept
{
    if (! columns.empty() && currentFrame().averages > 0)
        return true;

    for (const auto& trace : overlays)
        if (trace.columns != nullptr && ! trace.columns->empty())
            return true;

    return false;
}

void FrequencyGraphView::setLiveFrame (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                                       double delaySamples, bool rawPhase)
{
    current = frame;
    currentFrameShared.reset();
    cachedLivePoints.reset();
    appliedDelaySamples = delaySamples;
    showRawPhase = rawPhase;
    livePointsAreCached = false;
    smoothingDenom = smoothingOctDenom;
    dataDirty = true;
    repaint();
}

void FrequencyGraphView::setLiveFrameCached (std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> points,
                                             std::shared_ptr<const eeq::dsp::SpectrumFrame> frame,
                                             int smoothingOctDenom,
                                             double delaySamples, bool rawPhase)
{
    if (points == nullptr || frame == nullptr)
        return;

    const bool changed = cachedLivePoints != points || currentFrameShared != frame
                      || smoothingOctDenom != smoothingDenom || rawPhase != showRawPhase
                      || std::abs (delaySamples - appliedDelaySamples) > 1.0e-9 || ! livePointsAreCached;

    if (! changed)
        return; // 何も変わっていなければ再描画しない（issue #135）

    if (cachedLivePoints != points)
    {
        columns.resize (points->size());

        for (size_t i = 0; i < points->size(); ++i)
        {
            const auto& point = (*points)[i];
            columns[i].freq = point.freq;
            columns[i].h = { (float) point.h.real(), (float) point.h.imag() };
            columns[i].coherence = point.coherence;
        }
    }

    currentFrameShared = std::move (frame);
    cachedLivePoints = std::move (points);
    livePointsAreCached = true;
    appliedDelaySamples = delaySamples;
    showRawPhase = rawPhase;
    smoothingDenom = smoothingOctDenom;
    dataDirty = false;
    repaint();
}

void FrequencyGraphView::clearLive()
{
    current = {};
    currentFrameShared.reset();
    cachedLivePoints.reset();
    columns.clear();
    livePointsAreCached = false;
    dataDirty = false;
    repaint();
}

void FrequencyGraphView::setOverlayTraces (std::vector<OverlayTrace> traces)
{
    overlays = std::move (traces);
    repaint();
}

juce::Rectangle<float> FrequencyGraphView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (dBLabelStripWidth + 6).withTrimmedRight (10)
               .withTrimmedTop (6).withTrimmedBottom (22);
}

// 段の配分（issue #144）: 3段 = 振幅 55% / 位相 27% / コヒーレンス 18%、2段 = 振幅 60% / 位相 40%
juce::Rectangle<float> FrequencyGraphView::amplitudeArea (const juce::Rectangle<float>& plot) const
{
    constexpr float gap = 6.0f;

    switch (viewMode)
    {
        case ViewMode::ampOnly:  return plot;
        case ViewMode::ampPhase: return plot.withHeight (plot.getHeight() * 0.60f - gap * 0.5f);
        case ViewMode::ampPhaseCoh:
        default:                 return plot.withHeight (plot.getHeight() * 0.55f - gap * 0.5f);
    }
}

juce::Rectangle<float> FrequencyGraphView::phaseArea (const juce::Rectangle<float>& plot) const
{
    constexpr float gap = 6.0f;

    switch (viewMode)
    {
        case ViewMode::ampOnly:  return {};
        case ViewMode::ampPhase: return plot.withTrimmedTop (plot.getHeight() * 0.60f + gap * 0.5f);
        case ViewMode::ampPhaseCoh:
        default:
        {
            const auto top = plot.getY() + plot.getHeight() * 0.55f + gap * 0.5f;
            const auto bottom = plot.getY() + plot.getHeight() * 0.82f - gap * 0.5f;
            return { plot.getX(), top, plot.getWidth(), juce::jmax (0.0f, bottom - top) };
        }
    }
}

juce::Rectangle<float> FrequencyGraphView::coherenceArea (const juce::Rectangle<float>& plot) const
{
    constexpr float gap = 6.0f;

    if (viewMode != ViewMode::ampPhaseCoh)
        return {};

    return plot.withTrimmedTop (plot.getHeight() * 0.82f + gap * 0.5f);
}

void FrequencyGraphView::rebuildLive()
{
    const auto& liveFrame = currentFrame();
    dataDirty = false;
    lastWidth = getWidth();
    columns.clear();

    if (liveFrame.fftSize == 0 || liveFrame.sampleRate <= 0.0 || liveFrame.averages == 0
        || liveFrame.magnitudeDb.empty() || liveFrame.transfer.size() != liveFrame.magnitudeDb.size())
        return;

    const auto effectiveMax = std::min (axis.viewMax, liveFrame.sampleRate * 0.5);
    const auto points = eeq::dsp::smoothToLogGrid (liveFrame, smoothingDenom, axis.viewMin, effectiveMax,
                                                   getPlotWidthForCache());

    columns.resize (points.size());

    for (size_t i = 0; i < points.size(); ++i)
    {
        columns[i].freq = points[i].freq;
        columns[i].h = { (float) points[i].h.real(), (float) points[i].h.imag() };
        columns[i].coherence = points[i].coherence;
    }
}

void FrequencyGraphView::resized()
{
    if (! livePointsAreCached)
        dataDirty = true;
}

void FrequencyGraphView::viewChanged()
{
    if (! livePointsAreCached)
        dataDirty = true;

    if (onViewChanged)
        onViewChanged();

    repaint();
}

int FrequencyGraphView::nearestColumn (double freq) const
{
    if (columns.empty() || freq <= 0.0)
        return -1;

    // columns は周波数の昇順
    const auto it = std::lower_bound (columns.begin(), columns.end(), freq,
                                      [] (const DisplayColumn& c, double f) { return c.freq < f; });

    if (it == columns.end())
        return (int) columns.size() - 1;

    const auto index = (int) std::distance (columns.begin(), it);

    if (index > 0 && std::abs (std::log (columns[(size_t) index - 1].freq / freq))
                         < std::abs (std::log (it->freq / freq)))
        return index - 1;

    return index;
}

float FrequencyGraphView::livePhaseDeg (const DisplayColumn& column) const
{
    return phaseDegOf (column.h, column.freq, showRawPhase ? 0.0 : appliedDelaySamples, currentFrame().sampleRate);
}

void FrequencyGraphView::mouseMove (const juce::MouseEvent& event)
{
    const auto area = plotArea();
    const auto newHover = area.contains (event.position) ? axis.xToFreq (event.position.x, area) : -1.0;

    if (std::abs (newHover - hoverFreq) > 1.0e-9)
    {
        hoverFreq = newHover;
        repaint();
    }
}

void FrequencyGraphView::mouseExit (const juce::MouseEvent&)
{
    if (hoverFreq > 0.0)
    {
        hoverFreq = -1.0;
        repaint();
    }
}

void FrequencyGraphView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto plot = plotArea();
    const auto amp = amplitudeArea (plot);

    // dBラベル帯の上では dB 軸をズーム、それ以外は周波数軸（仕様 5.2）
    if (wheel.deltaY != 0.0f && event.position.x < (float) dBLabelStripWidth && amp.getHeight() > 0.0f)
    {
        ampAuto = false;
        const auto anchor = ampTop - (event.position.y - amp.getY()) / amp.getHeight() * ampSpan;
        const auto factor = wheel.deltaY > 0 ? 0.85f : 1.18f;
        ampSpan = juce::jlimit (6.0f, 300.0f, ampSpan * factor);
        ampTop = anchor + (event.position.y - amp.getY()) / amp.getHeight() * ampSpan;
        repaint();
    }
    else if (wheel.deltaY != 0.0f && plot.contains (event.position))
    {
        if (axis.zoom (event.position.x, plot, wheel.deltaY))
            viewChanged();
    }
}

void FrequencyGraphView::mouseDown (const juce::MouseEvent& event)
{
    dragStart = event.position;
    dragStartAmpTop = ampTop;
    dragStartViewMin = axis.viewMin;
    dragStartViewMax = axis.viewMax;
    dragging = false;

    const auto plot = plotArea();

    if (event.mods.isPopupMenu() && plot.contains (event.position))
        showContextMenu (axis.xToFreq (event.position.x, plot));
}

void FrequencyGraphView::mouseDrag (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    const auto plot = plotArea();
    const auto dx = event.position.x - dragStart.x;
    const auto dy = event.position.y - dragStart.y;

    if (! dragging && std::abs (dx) < 3.0f && std::abs (dy) < 3.0f)
        return;

    dragging = true;

    // 横ドラッグ: 周波数のパン（仕様 5.2）。開始時の表示範囲からの総移動量で決める（issue #129）
    if (std::abs (dx) > std::abs (dy))
    {
        axis.viewMin = dragStartViewMin;
        axis.viewMax = dragStartViewMax;
        axis.pan (dx, plot);
        viewChanged();
    }
    else if (! ampAuto)
    {
        // 縦ドラッグ: dB軸のパン
        const auto amp = amplitudeArea (plot);

        if (amp.getHeight() > 0.0f)
        {
            ampTop = dragStartAmpTop + dy / amp.getHeight() * ampSpan;
            repaint();
        }
    }
}

void FrequencyGraphView::mouseUp (const juce::MouseEvent& event)
{
    // Shift+クリックでマーカーを置く（issue #145）
    if (! dragging && event.mods.isShiftDown() && ! event.mods.isPopupMenu())
    {
        const auto plot = plotArea();

        if (plot.contains (event.position))
            addMarker (axis.xToFreq (event.position.x, plot));
    }

    dragging = false;
}

void FrequencyGraphView::mouseDoubleClick (const juce::MouseEvent&)
{
    resetView();
}

void FrequencyGraphView::resetView()
{
    axis.resetView();

    if (scalePreset == ScalePreset::autoScale)
        ampAuto = true;
    else
        setScalePreset (scalePreset);

    viewChanged();
}

void FrequencyGraphView::showContextMenu (double freq)
{
    juce::PopupMenu menu;
    menu.addItem (1, strings::markerAddHere);
    menu.addItem (2, strings::markerAddPeak);
    menu.addItem (3, strings::markerRemoveNear, ! markers.empty());
    menu.addItem (4, strings::markerClearAll, ! markers.empty());
    menu.addSeparator();
    menu.addItem (5, strings::graphResetView);

    juce::Component::SafePointer<FrequencyGraphView> safeThis { this };
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safeThis, freq] (int result)
                        {
                            if (safeThis == nullptr)
                                return;

                            switch (result)
                            {
                                case 1: safeThis->addMarker (freq); break;
                                case 2: safeThis->addMarkerAtPeak(); break;
                                case 3: safeThis->removeMarkerNear (freq); break;
                                case 4: safeThis->clearMarkers(); break;
                                case 5: safeThis->resetView(); break;
                                default: break;
                            }
                        });
}

void FrequencyGraphView::setViewMode (ViewMode mode)
{
    if (viewMode != mode)
    {
        viewMode = mode;
        repaint();
    }
}

void FrequencyGraphView::setScalePreset (ScalePreset preset)
{
    scalePreset = preset;

    if (preset == ScalePreset::autoScale)
    {
        ampAuto = true;
        repaint();
        return;
    }

    const auto range = preset == ScalePreset::preset6dB ? 6.0f
                     : preset == ScalePreset::preset12dB ? 12.0f
                     : preset == ScalePreset::preset24dB ? 24.0f
                                                         : 48.0f;

    // 中心は表示範囲内のライブ曲線の平均レベル（6 dB 単位に丸める）。データがなければ 0 dB
    float centre = 0.0f;
    double sum = 0.0;
    int count = 0;

    for (const auto& column : columns)
    {
        if (column.freq < axis.viewMin || column.freq > axis.viewMax)
            continue;

        const auto db = ampDbOf (column.h);

        if (std::isfinite (db) && db > -200.0f)
        {
            sum += db;
            ++count;
        }
    }

    if (count > 0)
        centre = std::round ((float) (sum / count) / 6.0f) * 6.0f;

    ampAuto = false;
    ampSpan = range * 2.0f;
    ampTop = centre + range;
    repaint();
}

void FrequencyGraphView::setCoherenceBlanking (bool enabled)
{
    if (coherenceBlankingEnabled != enabled)
    {
        coherenceBlankingEnabled = enabled;
        repaint();
    }
}

void FrequencyGraphView::setCoherenceThreshold (float threshold)
{
    threshold = juce::jlimit (0.0f, 1.0f, threshold);

    if (std::abs (threshold - coherenceThreshold) > 1.0e-6f)
    {
        coherenceThreshold = threshold;
        repaint();
    }
}

void FrequencyGraphView::setClipWarning (bool clipped)
{
    if (clipWarning != clipped)
    {
        clipWarning = clipped;
        repaint();
    }
}

void FrequencyGraphView::addMarker (double freq)
{
    if (freq < axis.limitMin || freq > axis.limitMax)
        return;

    markers.push_back ({ freq, markerColour (nextMarkerColourIndex++) });
    repaint();
}

void FrequencyGraphView::addMarkerAtPeak()
{
    int best = -1;
    float bestDb = -1.0e9f;

    for (int i = 0; i < (int) columns.size(); ++i)
    {
        const auto& column = columns[(size_t) i];

        if (column.freq < axis.viewMin || column.freq > axis.viewMax)
            continue;

        const auto db = ampDbOf (column.h);

        if (std::isfinite (db) && db > bestDb)
        {
            bestDb = db;
            best = i;
        }
    }

    if (best >= 0)
        addMarker (columns[(size_t) best].freq);
}

void FrequencyGraphView::removeMarkerNear (double freq)
{
    if (markers.empty() || freq <= 0.0)
        return;

    size_t nearest = 0;

    for (size_t i = 1; i < markers.size(); ++i)
        if (std::abs (std::log (markers[i].freq / freq)) < std::abs (std::log (markers[nearest].freq / freq)))
            nearest = i;

    markers.erase (markers.begin() + (std::ptrdiff_t) nearest);
    repaint();
}

void FrequencyGraphView::clearMarkers()
{
    if (! markers.empty())
    {
        markers.clear();
        repaint();
    }
}

void FrequencyGraphView::updateAutoScale()
{
    if (! ampAuto)
        return;

    float minAmp = 1.0e9f, maxAmp = -1.0e9f;

    auto consider = [&] (float amp)
    {
        if (std::isfinite (amp) && amp > -200.0f)
        {
            minAmp = std::min (minAmp, amp);
            maxAmp = std::max (maxAmp, amp);
        }
    };

    if (currentFrame().averages > 0)
        for (const auto& column : columns)
            consider (ampDbOf (column.h));

    // ライブがないときは重ね描きの範囲で合わせる（保存済みスナップショットの見返し: issue #130）
    if (maxAmp < minAmp)
        for (const auto& trace : overlays)
            if (trace.columns != nullptr)
                for (const auto& column : *trace.columns)
                    if (column.freq >= axis.viewMin && column.freq <= axis.viewMax)
                        consider (ampDbOf (column.h) + (float) trace.dbOffset);

    if (maxAmp > minAmp)
    {
        // オートスケールは目盛りが揺れないよう 10 dB 単位で決め、余裕を持って切り替える
        const auto targetTop = std::ceil ((maxAmp + 6.0f) / 10.0f) * 10.0f;
        const auto targetSpan = juce::jlimit (40.0f, 160.0f, std::ceil (((maxAmp - minAmp) + 20.0f) / 10.0f) * 10.0f);

        if (std::abs (targetTop - ampTop) >= 10.0f || maxAmp > ampTop - 2.0f || minAmp < ampTop - ampSpan + 2.0f)
            ampTop = targetTop;

        if (targetSpan > ampSpan || targetSpan < ampSpan - 30.0f)
            ampSpan = targetSpan;
    }
}

void FrequencyGraphView::paint (juce::Graphics& g)
{
    if (! livePointsAreCached && (dataDirty || getWidth() != lastWidth))
        rebuildLive();

    const auto& liveFrame = currentFrame();
    const bool haveLive = liveFrame.averages > 0 && ! columns.empty();

    const auto bounds = getLocalBounds().toFloat();
    const auto plot = plotArea();
    const auto ampArea = amplitudeArea (plot);
    const auto phaseRect = phaseArea (plot);
    const auto cohRect = coherenceArea (plot);
    const auto grid = theme::get (theme::ColourId::grid);
    const auto gridMajor = theme::get (theme::ColourId::gridMajor);
    const auto gridText = theme::get (theme::ColourId::gridText);

    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (bounds, 6.0f);

    // ---- 周波数グリッド（全段で共有: 仕様 5.1）----
    axis.drawGrid (g, plot, grid, gridMajor, gridText);

    updateAutoScale();

    // 段の区切り
    g.setColour (gridMajor.withAlpha (0.6f));
    if (! phaseRect.isEmpty())
        g.drawHorizontalLine ((int) (ampArea.getBottom() + 3.0f), plot.getX(), plot.getRight());
    if (! cohRect.isEmpty())
        g.drawHorizontalLine ((int) (phaseRect.getBottom() + 3.0f), plot.getX(), plot.getRight());

    // ---- 振幅エリア: dBグリッド ----
    const auto ampY = [&ampArea, top = ampTop, span = ampSpan] (float db)
    { return ampArea.getY() + (top - db) / span * ampArea.getHeight(); };

    const auto dbStep = ampSpan > 150.0f ? 20 : ampSpan > 60.0f ? 10 : ampSpan > 24.0f ? 6 : 3;
    g.setFont (juce::Font (juce::FontOptions (10.5f)));

    for (int db = -240; db <= 120; db += dbStep)
    {
        const auto y = ampY ((float) db);

        if (y < ampArea.getY() - 1 || y > ampArea.getBottom() + 1)
            continue;

        g.setColour (grid);
        g.drawLine (ampArea.getX(), y, ampArea.getRight(), y, 1.0f);
        g.setColour (gridText);
        g.drawText (juce::String (db), 2, (int) y - 7, dBLabelStripWidth - 2, 14, juce::Justification::centredRight);
    }

    // ---- 位相エリア ----
    const auto phaseY = [&phaseRect] (float deg)
    { return phaseRect.getY() + (180.0f - deg) / 360.0f * phaseRect.getHeight(); };

    if (! phaseRect.isEmpty())
    {
        for (int deg = -180; deg <= 180; deg += 90)
        {
            const auto y = phaseY ((float) deg);
            g.setColour (grid);
            g.drawLine (phaseRect.getX(), y, phaseRect.getRight(), y, 1.0f);

            if (phaseRect.getHeight() > 50.0f || deg % 180 == 0)
            {
                g.setColour (gridText);
                g.drawText (juce::String (deg), 2, (int) y - 7, dBLabelStripWidth - 2, 14, juce::Justification::centredRight);
            }
        }
    }

    // ---- コヒーレンスエリア ----
    const auto cohY = [&cohRect] (float c) { return cohRect.getBottom() - c * cohRect.getHeight(); };

    if (! cohRect.isEmpty())
    {
        for (const auto c : { 0.0f, 0.5f, 1.0f })
        {
            const auto y = cohY (c);
            g.setColour (grid);
            g.drawLine (cohRect.getX(), y, cohRect.getRight(), y, 1.0f);
            g.setColour (gridText);
            g.drawText (c == 1.0f ? strings::gammaSquared + " 1" : juce::String (c, 1),
                        2, (int) y - 7, dBLabelStripWidth - 2, 14, juce::Justification::centredRight);
        }

        g.setColour (theme::get (theme::ColourId::warn).withAlpha (0.45f));
        const float dashes[] = { 4.0f, 4.0f };
        const auto thY = cohY (coherenceThreshold);
        g.drawDashedLine ({ cohRect.getX(), thY, cohRect.getRight(), thY }, dashes, 2, 1.0f);
    }

    if (! hasAnythingToDraw())
    {
        g.setColour (gridText);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText (strings::graphPlaceholder, getLocalBounds(), juce::Justification::centred);
        return;
    }

    // ---- カーブ描画ヘルパ ----
    // isBright が false の区間: ブランキング有効なら描かない、無効なら薄く描く（仕様 3.1 / 5.5）
    auto strokeCurve = [&] (const auto& curveColumns, const juce::Rectangle<float>& area, juce::Colour colour,
                            float thickness, bool dashed, bool wrapAware, auto&& valueOf, auto&& yOf, auto&& isBright)
    {
        if (area.isEmpty() || curveColumns.empty())
            return;

        const juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestInt());

        juce::Path dimPath, brightPath;
        bool inDim = false, inBright = false;
        float prevValue = 0.0f;
        bool havePrev = false;

        for (const auto& column : curveColumns)
        {
            if (column.freq < axis.viewMin * 0.98 || column.freq > axis.viewMax * 1.02)
            {
                inDim = inBright = havePrev = false;
                continue;
            }

            const auto value = valueOf (column);

            if (! std::isfinite (value))
            {
                inDim = inBright = havePrev = false;
                continue;
            }

            const auto x = axis.freqToX (column.freq, area);
            const auto y = yOf (value);
            const bool bright = isBright (column);
            // 位相の ±180° 折り返しでは線をつながない（issue #56）
            const bool wraps = wrapAware && havePrev && std::abs (value - prevValue) > 180.0f;

            auto& path = bright ? brightPath : dimPath;
            auto& inPath = bright ? inBright : inDim;

            if (! inPath || wraps)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);

            inPath = true;
            (bright ? inDim : inBright) = false;
            prevValue = value;
            havePrev = true;
        }

        if (dashed)
        {
            juce::Path combined = brightPath;
            combined.addPath (dimPath);
            juce::Path dashedPath;
            const float pattern[] = { 5.0f, 4.0f };
            juce::PathStrokeType (thickness).createDashedStroke (dashedPath, combined, pattern, 2);
            g.setColour (colour);
            g.fillPath (dashedPath);
            return;
        }

        if (! coherenceBlankingEnabled)
        {
            g.setColour (colour.withAlpha (dimmedAlpha));
            g.strokePath (dimPath, juce::PathStrokeType (thickness));
        }

        g.setColour (colour);
        g.strokePath (brightPath, juce::PathStrokeType (thickness + 0.4f));
    };

    const auto always = [] (const auto&) { return true; };

    // ---- 重ね描き（先に描いてライブを目立たせる）----
    for (const auto& trace : overlays)
    {
        if (trace.columns == nullptr || trace.columns->empty())
            continue;

        const auto offset = (float) trace.dbOffset;
        strokeCurve (*trace.columns, ampArea, trace.colour, trace.thickness, trace.dashed, false,
                     [offset] (const OverlayColumn& c) { return ampDbOf (c.h) + offset; }, ampY, always);

        if (trace.showPhase && ! phaseRect.isEmpty())
        {
            const auto delay = trace.delayOffsetSamples;
            const auto sr = trace.sampleRate;
            strokeCurve (*trace.columns, phaseRect, trace.colour.withMultipliedAlpha (0.8f), trace.thickness,
                         trace.dashed, true,
                         [delay, sr] (const OverlayColumn& c) { return phaseDegOf (c.h, c.freq, delay, sr); },
                         phaseY, always);
        }
    }

    // ---- ライブ ----
    if (haveLive)
    {
        const auto liveColour = theme::get (theme::ColourId::accent);
        const auto livePhaseColour = theme::get (theme::ColourId::accent2);
        const auto thr = coherenceThreshold;
        const auto isBright = [thr] (const DisplayColumn& c) { return c.coherence >= thr; };

        if (! cohRect.isEmpty())
            strokeCurve (columns, cohRect, gridText.withAlpha (0.8f), 1.0f, false, false,
                         [] (const DisplayColumn& c) { return juce::jlimit (0.0f, 1.0f, c.coherence); }, cohY, always);

        strokeCurve (columns, ampArea, liveColour, 1.3f, false, false,
                     [] (const DisplayColumn& c) { return ampDbOf (c.h); }, ampY, isBright);

        if (! phaseRect.isEmpty())
            strokeCurve (columns, phaseRect, livePhaseColour, 1.2f, false, true,
                         [this] (const DisplayColumn& c) { return livePhaseDeg (c); }, phaseY, isBright);

        g.setColour (liveColour);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("LIVE", (int) ampArea.getRight() - 48, (int) ampArea.getY() + 28, 44, 14,
                    juce::Justification::centredRight);
    }

    // ---- 凡例（左上）----
    struct LegendEntry { juce::String name; juce::Colour colour; bool dashed; };
    std::vector<LegendEntry> legend;

    if (haveLive)
        legend.push_back ({ strings::liveMeasurementLabel, theme::get (theme::ColourId::accent), false });

    for (const auto& trace : overlays)
        if (trace.columns != nullptr && ! trace.columns->empty())
        {
            auto name = trace.name;

            if (std::abs (trace.dbOffset) > 1.0e-6)
                name << " (" << (trace.dbOffset > 0 ? "+" : "") << juce::String (trace.dbOffset, 1) << " dB)";

            legend.push_back ({ name, trace.colour, trace.dashed });
        }

    if (! legend.empty())
    {
        const juce::Font font (juce::FontOptions (11.5f));
        g.setFont (font);
        float maxTextWidth = 0.0f;

        for (const auto& entry : legend)
            maxTextWidth = juce::jmax (maxTextWidth, juce::GlyphArrangement::getStringWidth (font, entry.name));

        constexpr float lineLen = 24.0f, rowHeight = 17.0f;
        const auto boxWidth = juce::jmin (ampArea.getWidth() * 0.5f, lineLen + maxTextWidth + 26.0f);
        const auto maxRows = juce::jmax (1, (int) ((ampArea.getHeight() - 12.0f) / rowHeight));
        const auto rows = juce::jmin ((int) legend.size(), maxRows);
        const juce::Rectangle<float> box (ampArea.getX() + 6.0f, ampArea.getY() + 6.0f, boxWidth, rows * rowHeight + 8.0f);

        g.setColour (overlayBoxColour (0.82f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (theme::get (theme::ColourId::controlOutline).withAlpha (0.6f));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);

        for (int i = 0; i < rows; ++i)
        {
            const auto& entry = legend[(size_t) i];
            const auto y = box.getY() + 4.0f + (float) i * rowHeight + rowHeight * 0.5f;
            const auto x = box.getX() + 6.0f;

            g.setColour (entry.colour);

            if (entry.dashed)
            {
                const float pattern[] = { 5.0f, 3.0f };
                g.drawDashedLine ({ x, y, x + lineLen, y }, pattern, 2, 1.6f);
            }
            else
                g.drawLine (x, y, x + lineLen, y, 2.0f);

            g.setColour (theme::get (theme::ColourId::text));
            const auto text = (i == rows - 1 && rows < (int) legend.size())
                                  ? juce::String (u8"… ほか ") + juce::String ((int) legend.size() - rows + 1)
                                  : entry.name;
            g.drawText (text, (int) (x + lineLen + 6.0f), (int) (y - rowHeight * 0.5f),
                        (int) (box.getRight() - x - lineLen - 10.0f), (int) rowHeight, juce::Justification::centredLeft);
        }
    }

    drawMarkers (g, plot, ampArea);
    drawCursor (g, plot, ampArea);

    // ---- 入力クリップ警告（issue #146）----
    if (clipWarning)
    {
        const auto box = juce::Rectangle<float> (ampArea.getCentreX() - 150.0f, ampArea.getY() + 6.0f, 300.0f, 22.0f);
        g.setColour (theme::get (theme::ColourId::danger).withAlpha (0.9f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (strings::graphClipWarning, box, juce::Justification::centred);
    }
}

void FrequencyGraphView::drawCursor (juce::Graphics& g, const juce::Rectangle<float>& plot,
                                     const juce::Rectangle<float>& ampArea)
{
    if (hoverFreq <= 0.0)
        return;

    const auto x = axis.freqToX (hoverFreq, plot);
    g.setColour (theme::get (theme::ColourId::text).withAlpha (0.3f));
    g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.0f);

    // 読み取り: ライブ + 各重ね描き（issue #145: 全トレースの値と、ライブとの差）
    juce::StringArray lines;
    float liveDb = std::numeric_limits<float>::quiet_NaN();
    const auto liveIndex = currentFrame().averages > 0 ? nearestColumn (hoverFreq) : -1;

    if (liveIndex >= 0)
    {
        const auto& column = columns[(size_t) liveIndex];
        liveDb = ampDbOf (column.h);
        lines.add (LogFrequencyAxis::freqText (column.freq) + "   LIVE " + juce::String (liveDb, 1) + " dB   "
                   + juce::String (livePhaseDeg (column), 0) + strings::degreeSymbol + "   "
                   + strings::gammaSquared + " " + juce::String (column.coherence, 2));
    }
    else
    {
        lines.add (LogFrequencyAxis::freqText (hoverFreq));
    }

    for (const auto& trace : overlays)
    {
        if (trace.columns == nullptr || trace.columns->empty())
            continue;

        const auto& cols = *trace.columns;
        const auto it = std::lower_bound (cols.begin(), cols.end(), hoverFreq,
                                          [] (const OverlayColumn& c, double f) { return c.freq < f; });

        if (it == cols.end() || it->freq > hoverFreq * 1.1 || (it != cols.begin() && (it - 1)->freq < hoverFreq * 0.9 && it->freq > hoverFreq * 1.1))
            continue;

        const auto db = ampDbOf (it->h) + (float) trace.dbOffset;
        auto line = trace.name + "  " + juce::String (db, 1) + " dB";

        if (std::isfinite (liveDb))
            line << "  (" << strings::cursorDeltaLive << " " << (liveDb - db >= 0 ? "+" : "") << juce::String (liveDb - db, 1) << ")";

        lines.add (line);

        if (lines.size() >= 6)
            break;
    }

    const juce::Font font (juce::FontOptions (11.5f));
    float width = 0.0f;

    for (const auto& line : lines)
        width = juce::jmax (width, juce::GlyphArrangement::getStringWidth (font, line));

    width = juce::jmin (width + 16.0f, ampArea.getWidth() - 8.0f);
    const auto height = (float) lines.size() * 16.0f + 6.0f;
    const juce::Rectangle<float> box (ampArea.getRight() - width - 4.0f, ampArea.getY() + 4.0f, width, height);

    g.setColour (overlayBoxColour (0.85f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (font);

    for (int i = 0; i < lines.size(); ++i)
        g.drawText (lines[i], box.withTrimmedLeft (8.0f).withY (box.getY() + 3.0f + (float) i * 16.0f).withHeight (16.0f),
                    juce::Justification::centredLeft);
}

void FrequencyGraphView::drawMarkers (juce::Graphics& g, const juce::Rectangle<float>& plot,
                                      const juce::Rectangle<float>& ampArea)
{
    if (markers.empty())
        return;

    const bool haveLive = currentFrame().averages > 0 && ! columns.empty();
    juce::StringArray lines;
    float firstDb = 0.0f, firstPhase = 0.0f;
    double firstFreq = 0.0;
    bool haveFirst = false;

    for (size_t m = 0; m < markers.size(); ++m)
    {
        const auto& marker = markers[m];

        if (marker.freq >= axis.viewMin && marker.freq <= axis.viewMax)
        {
            const auto x = axis.freqToX (marker.freq, plot);
            g.setColour (marker.colour.withAlpha (0.85f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), 1.2f);
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText ("M" + juce::String ((int) m + 1), (int) x + 2, (int) plot.getBottom() - 14, 30, 12,
                        juce::Justification::centredLeft);
        }

        // 値はライブ曲線から毎回読む（Smaart と同様にマーカーは周波数に固定）
        auto line = "M" + juce::String ((int) m + 1) + "  " + LogFrequencyAxis::freqText (marker.freq);
        const auto index = haveLive ? nearestColumn (marker.freq) : -1;

        if (index >= 0)
        {
            const auto& column = columns[(size_t) index];
            const auto db = ampDbOf (column.h);
            const auto phase = livePhaseDeg (column);
            line << "  " << juce::String (db, 1) << " dB  " << juce::String (phase, 0) << strings::degreeSymbol;

            if (! haveFirst)
            {
                haveFirst = true;
                firstDb = db;
                firstPhase = phase;
                firstFreq = marker.freq;
            }
            else
            {
                const auto dPhase = (float) std::remainder ((double) (phase - firstPhase), 360.0);
                line << "   " << juce::String (u8"Δ") << juce::String (std::log2 (marker.freq / firstFreq), 2) << " oct "
                     << (db - firstDb >= 0 ? "+" : "") << juce::String (db - firstDb, 1) << " dB "
                     << juce::String (dPhase, 0) << strings::degreeSymbol;
            }
        }

        lines.add (line);
    }

    const juce::Font font (juce::FontOptions (11.0f));
    float width = 0.0f;

    for (const auto& line : lines)
        width = juce::jmax (width, juce::GlyphArrangement::getStringWidth (font, line));

    width = juce::jmin (width + 16.0f, ampArea.getWidth() - 8.0f);
    const auto height = (float) lines.size() * 15.0f + 6.0f;
    const juce::Rectangle<float> box (ampArea.getX() + 6.0f, ampArea.getBottom() - height - 4.0f, width, height);

    g.setColour (overlayBoxColour (0.85f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setFont (font);

    for (int i = 0; i < lines.size(); ++i)
    {
        g.setColour (markers[(size_t) i].colour.interpolatedWith (theme::get (theme::ColourId::text), 0.35f));
        g.drawText (lines[i], box.withTrimmedLeft (8.0f).withY (box.getY() + 3.0f + (float) i * 15.0f).withHeight (15.0f),
                    juce::Justification::centredLeft);
    }
}

} // namespace eeq::ui
