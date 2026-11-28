#pragma once

#include "Core/JuceModules.h"

#include "../DSP/OctaveSmoother.h"
#include "../DSP/TransferFunctionAccumulator.h"
#include "LogFrequencyGraph.h"

#include <complex>
#include <functional>
#include <memory>
#include <vector>

namespace eeq::ui
{

// 表示モード（issue #144）
enum class ViewMode
{
    ampPhaseCoh,  // 振幅/位相/コヒーレンス (3段)
    ampPhase,     // 振幅/位相 (2段)
    ampOnly       // 振幅のみ (1段)
};

// 縦スケールプリセット（issue #144）。±N は表示中心からの範囲
enum class ScalePreset
{
    autoScale,
    preset6dB,
    preset12dB,
    preset24dB,
    preset48dB
};

// メイン表示（仕様 5.1）: 振幅・位相・コヒーレンスを最大3段で表示する。対数周波数軸は全段で共有。
//
// このコンポーネントは「描画専用」で、重い平滑化はしない（issue #62 / #66）。
//  - ライブ曲線: タブ側のワーカーが平滑化した点列を setLiveFrameCached で受け取る
//  - 重ね描き:  タブ側が平滑化済みの列（周波数つき）を setOverlayTraces で渡す
// 曲線はすべて「列の周波数」から x を決める（インデックス等間隔にしない: issue #140）。
// 再描画はデータ・表示範囲・カーソルが変わったときだけ（issue #135）。
class FrequencyGraphView : public juce::Component
{
public:
    FrequencyGraphView();

    /** 重ね描きの1列（平滑化済み）。h は振幅・位相を持つ複素値 */
    struct OverlayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
    };

    /** 重ね描きする1本（スナップショット/ターゲット/予測/差分） */
    struct OverlayTrace
    {
        juce::String name;
        juce::Colour colour;
        float thickness = 1.2f;
        bool dashed = false;
        bool showPhase = false;
        double dbOffset = 0.0;           // 表示時の dB オフセット（issue #143）
        double delayOffsetSamples = 0.0; // 表示時の位相の遅延補正（issue #143）
        double sampleRate = 48000.0;     // 遅延オフセットの換算用
        std::shared_ptr<const std::vector<OverlayColumn>> columns;
    };

    /** 直接計算モード（小さいフレーム向け）。重いフレームはタブ側で setLiveFrameCached を使う */
    void setLiveFrame (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                       double appliedDelaySamples, bool showRawPhase);

    /** ワーカーの事前計算結果を共有し、新しい点列が届いたときだけ描画列へ変換する */
    void setLiveFrameCached (std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> points,
                             std::shared_ptr<const eeq::dsp::SpectrumFrame> frame,
                             int smoothingOctDenom,
                             double appliedDelaySamples, bool showRawPhase);

    /** ライブ曲線を消す（測定データなしの状態に戻す） */
    void clearLive();

    /** 重ね描きを差し替える（描画属性込み。issue #127） */
    void setOverlayTraces (std::vector<OverlayTrace> traces);

    /** 平滑化グリッドの範囲（タブ側ワーカーのキャッシュ鍵） */
    int getPlotWidthForCache() const noexcept { return juce::jmax (2, (int) plotArea().getWidth()); }
    double getViewMinForCache() const noexcept { return axis.viewMin; }
    double getViewMaxForCache() const noexcept { return axis.viewMax; }
    [[nodiscard]] int plotWidth() const noexcept { return getPlotWidthForCache(); }

    /** 表示モード（issue #144） */
    void setViewMode (ViewMode mode);
    ViewMode getViewMode() const noexcept { return viewMode; }

    /** 縦スケールプリセット（issue #144）。autoScale 以外は固定スケールになる */
    void setScalePreset (ScalePreset preset);
    ScalePreset getScalePreset() const noexcept { return scalePreset; }

    /** コヒーレンスブランキング（issue #144）: 有効ならしきい値未満の区間を描かない（無効なら薄く描く） */
    void setCoherenceBlanking (bool enabled);
    bool getCoherenceBlanking() const noexcept { return coherenceBlankingEnabled; }
    void setCoherenceThreshold (float threshold);
    float getCoherenceThreshold() const noexcept { return coherenceThreshold; }

    /** 入力クリップ警告（issue #146）: true の間グラフ上に警告を出す */
    void setClipWarning (bool clipped);
    /** 「この表示はまだ信用しきれない」理由（平均不足・Ref 無音・補正の不一致など。ロードマップ B-3） */
    void setNotices (const juce::StringArray& notices);

    /** 位相の遅延補正値（サンプル）と生位相表示 */
    double getAppliedDelaySamples() const noexcept { return appliedDelaySamples; }

    // ---- マーカー（issue #145）----
    struct Marker
    {
        double freq = 0.0;
        juce::Colour colour;
    };
    /** 周波数にマーカーを置く（値はライブ曲線から毎回読み取る） */
    void addMarker (double freq);
    /** 表示範囲内のライブ曲線の最大値にマーカーを置く */
    void addMarkerAtPeak();
    void removeMarkerNear (double freq);
    void clearMarkers();
    const std::vector<Marker>& getMarkers() const noexcept { return markers; }

    /** 表示範囲（ズーム/パン）が変わったとき */
    std::function<void()> onViewChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // ---- テスト用の読み出し ----
    [[nodiscard]] juce::Rectangle<float> getPlotAreaForTest() const { return plotArea(); }
    [[nodiscard]] juce::Rectangle<float> getAmplitudeAreaForTest() const { return amplitudeArea (plotArea()); }
    [[nodiscard]] juce::Rectangle<float> getPhaseAreaForTest() const { return phaseArea (plotArea()); }
    [[nodiscard]] juce::Rectangle<float> getCoherenceAreaForTest() const { return coherenceArea (plotArea()); }
    [[nodiscard]] size_t getNumOverlayTraces() const noexcept { return overlays.size(); }
    [[nodiscard]] LogFrequencyAxis& getAxisForTest() noexcept { return axis; }
    [[nodiscard]] bool hasAnythingToDraw() const noexcept;

private:
    struct DisplayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
        float coherence = 0.0f;
    };

    void rebuildLive();
    const eeq::dsp::SpectrumFrame& currentFrame() const noexcept
    { return currentFrameShared != nullptr ? *currentFrameShared : current; }

    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> amplitudeArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> phaseArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> coherenceArea (const juce::Rectangle<float>& plot) const;

    void viewChanged();
    void resetView();
    void updateAutoScale();
    [[nodiscard]] float livePhaseDeg (const DisplayColumn& column) const;
    [[nodiscard]] int nearestColumn (double freq) const;
    void drawMarkers (juce::Graphics&, const juce::Rectangle<float>& plot, const juce::Rectangle<float>& ampArea);
    void drawCursor (juce::Graphics&, const juce::Rectangle<float>& plot, const juce::Rectangle<float>& ampArea);
    void showContextMenu (double freq);

    LogFrequencyAxis axis;

    eeq::dsp::SpectrumFrame current;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> currentFrameShared;
    std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> cachedLivePoints;
    std::vector<DisplayColumn> columns;
    double appliedDelaySamples = 0.0;
    bool showRawPhase = false;

    std::vector<OverlayTrace> overlays;

    int smoothingDenom = 6;
    int lastWidth = -1;
    bool livePointsAreCached = false;
    bool dataDirty = true;
    double hoverFreq = -1.0;

    // ドラッグ開始時の状態（パンは開始時からの総移動量で計算する: issue #129）
    juce::Point<float> dragStart;
    float dragStartAmpTop = 0.0f;
    double dragStartViewMin = 20.0, dragStartViewMax = 20000.0;
    bool dragging = false;

    // 振幅軸。auto は滑らかに追従、固定はホイール/ドラッグ/プリセットで調整（仕様 5.5）
    bool ampAuto = true;
    float ampTop = 10.0f;
    float ampSpan = 100.0f;

    ViewMode viewMode = ViewMode::ampPhaseCoh;
    ScalePreset scalePreset = ScalePreset::autoScale;
    bool coherenceBlankingEnabled = false;
    float coherenceThreshold = 0.8f; // 仕様 4.1 の既定しきい値
    bool clipWarning = false;
    juce::StringArray notices;

    std::vector<Marker> markers;
    int nextMarkerColourIndex = 0;

    static constexpr double freqLimitMin = 20.0;
    static constexpr double freqLimitMax = 20000.0;
    static constexpr float dimmedAlpha = 0.25f;
    static constexpr int dBLabelStripWidth = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrequencyGraphView)
};

} // namespace eeq::ui
