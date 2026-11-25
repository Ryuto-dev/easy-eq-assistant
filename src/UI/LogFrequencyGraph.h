#pragma once

#include <JuceHeader.h>

#include <cmath>

namespace eeq::ui
{

// 対数周波数軸の共通部品（仕様 5.2 / 5.5）。
// ズーム・パン（マウスホイールとドラッグ）、グリッドと主要周波数ラベルの描画を
// 複数のグラフビューで共有する。
struct LogFrequencyAxis
{
    double limitMin = 20.0;
    double limitMax = 20000.0;
    double viewMin = 20.0;
    double viewMax = 20000.0;

    void resetView() noexcept { viewMin = limitMin; viewMax = limitMax; }

    [[nodiscard]] bool isZoomed() const noexcept
    { return viewMin > limitMin * 1.0001 || viewMax < limitMax * 0.9999; }

    float freqToX (double freq, const juce::Rectangle<float>& area) const
    {
        const auto t = std::log (freq / viewMin) / std::log (viewMax / viewMin);
        return area.getX() + (float) juce::jlimit (0.0, 1.0, t) * area.getWidth();
    }

    double xToFreq (float x, const juce::Rectangle<float>& area) const
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - area.getX()) / std::max (1.0f, area.getWidth()));
        return viewMin * std::pow (viewMax / viewMin, (double) t);
    }

    /** ホイールでズームする（カーソル位置をアンカーに）。戻り値は表示範囲が変わったか */
    bool zoom (float x, const juce::Rectangle<float>& area, float wheelDelta)
    {
        const auto anchor = xToFreq (x, area);
        const auto factor = std::pow (1.15, wheelDelta > 0 ? 1.0 : -1.0);
        auto lo = anchor - (anchor - viewMin) * factor;
        auto hi = anchor + (viewMax - anchor) * factor;

        // 全体より広げない
        lo = juce::jlimit (limitMin, limitMax, lo);
        hi = juce::jlimit (limitMin, limitMax, hi);

        if (hi / lo < 1.05) // 最小ズーム幅
            return false;

        viewMin = lo;
        viewMax = hi;
        return true;
    }

    /** ドラッグでパンする。dx はピクセル移動量 */
    bool pan (float dxPixels, const juce::Rectangle<float>& area)
    {
        if (area.getWidth() <= 0.0f)
            return false;

        const auto octaves = std::log2 (viewMax / viewMin);
        const auto shift = -dxPixels / area.getWidth() * octaves;
        auto lo = viewMin * std::pow (2.0, (double) shift);
        auto hi = viewMax * std::pow (2.0, (double) shift);

        if (lo < limitMin)
        {
            lo = limitMin;
            hi = limitMin * std::pow (2.0, octaves);
        }

        if (hi > limitMax)
        {
            hi = limitMax;
            lo = limitMax / std::pow (2.0, octaves);
        }

        if (std::abs (lo - viewMin) < 1.0e-9)
            return false;

        viewMin = lo;
        viewMax = hi;
        return true;
    }

    /** 対数グリッドとラベル（20/50/100/200/500/1k/2k/5k/10k/20k: 仕様 5.5）を描く */
    void drawGrid (juce::Graphics& g, const juce::Rectangle<float>& area,
                   juce::Colour gridColour, juce::Colour majorColour, juce::Colour textColour,
                   bool drawLabels = true) const
    {
        g.setFont (juce::Font (juce::FontOptions (10.5f)));

        for (int p = 1; p <= 4; ++p)
        {
            for (int m = 1; m <= 9; ++m)
            {
                const auto f = (double) m * std::pow (10.0, (double) p);

                if (f < viewMin || f > viewMax)
                    continue;

                const auto x = freqToX (f, area);
                const bool major = (m == 1) || (m == 2) || (m == 5);

                g.setColour (major ? majorColour : gridColour);
                g.drawLine (x, area.getY(), x, area.getBottom(), 1.0f);

                if (drawLabels && major)
                {
                    g.setColour (textColour);
                    const auto text = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
                    g.drawText (text, (int) x - 20, (int) area.getBottom() + 2, 40, 14,
                                juce::Justification::centred);
                }
            }
        }
    }

    /** 周波数の読み取り表示用テキスト */
    [[nodiscard]] static juce::String freqText (double freq)
    {
        return freq >= 1000.0 ? juce::String (freq / 1000.0, 2) + " kHz" : juce::String (freq, 1) + " Hz";
    }
};

} // namespace eeq::ui
