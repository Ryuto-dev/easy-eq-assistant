#pragma once

#include "Core/JuceModules.h"

#include "../DSP/OctaveSmoother.h"
#include "../DSP/TransferFunctionAccumulator.h"
#include "LogFrequencyGraph.h"

#include <complex>
#include <vector>

namespace eeq::ui
{

// メイン表示（仕様 5.1）: 上段=振幅、下段=位相とコヒーレンス。対数周波数軸は上下で共有。
// スムージングは表示時に適用し、計算値は bin 分解能のまま保持する（仕様 3.1）。
// コヒーレンスがしきい値未満の帯域は薄く表示する（仕様 3.1 / 5.5）。
// ズーム・パン（周波数/dB 軸それぞれ独立）と、スナップショット・ターゲット・予測カーブの
// 重ね描きに対応（仕様 5.2）。
class FrequencyGraphView : public juce::Component
{
public:
    FrequencyGraphView();

    /** 重ね描きするカーブの素（スナップショット/ターゲット/予測。素は生の SpectrumFrame か点列） */
    struct OverlaySource
    {
        juce::String name;
        juce::Colour colour;
        const eeq::dsp::SpectrumFrame* frame = nullptr;      // スナップショット（生フレーム）
        const std::vector<eeq::dsp::GridPoint>* points = nullptr; // ターゲット/予測（点列）
        bool showPhase = false;
        float thickness = 1.2f;
        bool dashed = false;
    };

    /** 30Hz程度のタイマーから呼ぶ。smoothingOctDenom: 0=なし、3=1/3 oct、48=1/48 oct。
        appliedDelaySamples: 位相表示の遅延補正値（0なら生位相）。showRawPhase: 補正を無視して生位相 */
    void setLiveFrame (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                       double appliedDelaySamples, bool showRawPhase);

    /** 軽量版: timerTick で事前計算済みの GridPoint を受け取る（paint では単に columns へコピー） */
    void setLiveFrameCached (const std::vector<eeq::dsp::GridPoint>& points,
                             const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                             double appliedDelaySamples, bool showRawPhase);

    /** plotArea の幅を返す（キャッシュ計算用） */
    int getPlotWidthForCache() const noexcept { return juce::jmax (2, (int) plotArea().getWidth()); }

    /** オーバーレイを差し替える（version が変わったときだけ再計算する） */
    void setOverlays (const std::vector<OverlaySource>& sources, int version, int smoothingOctDenom);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** スナップショット等の平滑化に使うグリッド幅（タブ側の差分計算で揃える） */
    [[nodiscard]] int plotWidth() const noexcept { return juce::jmax (2, (int) plotArea().getWidth()); }

private:
    struct DisplayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
        float coherence = 0.0f;
    };

    struct OverlayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
    };

    void rebuildLive();
    void rebuildOverlays();

    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> amplitudeArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> lowerArea (const juce::Rectangle<float>& plot) const;

    LogFrequencyAxis axis;

    eeq::dsp::SpectrumFrame current;
    std::vector<DisplayColumn> columns;
    double appliedDelaySamples = 0.0;
    bool showRawPhase = false;

    // オーバーレイ（スナップショット・ターゲット・予測）
    std::vector<OverlaySource> overlaySources;
    std::vector<std::vector<OverlayColumn>> overlayColumns;
    std::vector<OverlayColumn> targetColumns; // 点列のオーバーレイ（freq/dB）
    int overlayVersion = 0;
    int lastOverlayVersion = -1;
    std::vector<int> overlaySmoothingPerSource; // 計算時のスムージング（変化検出用）

    int smoothingDenom = 6;
    int lastWidth = -1;
    bool dirty = true;
    int hoverColumn = -1;

    // ドラッグ開始位置（パン用）
    juce::Point<float> dragStart;
    float dragStartAmpTop = 0.0f;

    // 振幅軸。auto のときは滑らかに追従、固定のときはホイール/ドラッグで調整（仕様 5.5）
    bool ampAuto = true;
    float ampTop = 10.0f;
    float ampSpan = 100.0f;

    static constexpr double freqLimitMin = 20.0;
    static constexpr double freqLimitMax = 20000.0;
    static constexpr float coherenceThreshold = 0.8f; // 仕様 4.1 の既定しきい値
    static constexpr float dimmedAlpha = 0.25f;
    static constexpr int dBLabelStripWidth = 40;
};

} // namespace eeq::ui
