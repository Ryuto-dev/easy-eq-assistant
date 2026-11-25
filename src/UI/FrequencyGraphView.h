#pragma once

#include <JuceHeader.h>

#include "../DSP/TransferFunctionAccumulator.h"

#include <complex>
#include <vector>

namespace eeq::ui
{

// メイン表示（仕様 5.1）: 上段=振幅、下段=位相とコヒーレンス。対数周波数軸は上下で共有。
// スムージングは表示時に適用し、計算値は bin 分解能のまま保持する（仕様 3.1）。
// コヒーレンスがしきい値未満の帯域は薄く表示する（仕様 3.1 / 5.5）。
class FrequencyGraphView : public juce::Component
{
public:
    FrequencyGraphView();

    /** 30Hz程度のタイマーから呼ぶ。smoothingOctDenom: 0=なし、3=1/3 oct、48=1/48 oct */
    void setResult (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct DisplayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
        float coherence = 0.0f;
    };

    void rebuildDisplay();

    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> amplitudeArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> lowerArea (const juce::Rectangle<float>& plot) const;
    float freqToX (double freq, const juce::Rectangle<float>& area) const;
    double xToFreq (float x, const juce::Rectangle<float>& area) const;

    eeq::dsp::SpectrumFrame current;
    std::vector<DisplayColumn> columns;

    int smoothingDenom = 6;
    int lastWidth = -1;
    bool dirty = true;
    int hoverColumn = -1;

    // 振幅軸の自動スケール（滑らかに追従する）
    float ampTop = 10.0f;
    float ampSpan = 100.0f;

    static constexpr double freqMin = 20.0;
    static constexpr double freqMax = 20000.0;
    static constexpr float coherenceThreshold = 0.8f; // 仕様 4.1 の既定しきい値
};

} // namespace eeq::ui
