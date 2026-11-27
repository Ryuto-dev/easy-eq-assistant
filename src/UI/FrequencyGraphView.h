#pragma once

#include "Core/JuceModules.h"

#include "../DSP/OctaveSmoother.h"
#include "../DSP/TransferFunctionAccumulator.h"
#include "LogFrequencyGraph.h"

#include <complex>
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

// 縦スケールプリセット（issue #144）
enum class ScalePreset
{
    autoScale,
    preset6dB,    // ±6 dB
    preset12dB,   // ±12 dB
    preset24dB,   // ±24 dB
    preset48dB    // ±48 dB
};

// メイン表示（仕様 5.1）: 上段=振幅、下段=位相とコヒーレンス。対数周波数軸は上下で共有。
// スムージングは表示時に適用し、計算値は bin 分解能のまま保持する（仕様 3.1）。
// コヒーレンスがしきい値未満の帯域は薄く表示する（仕様 3.1 / 5.5）。
// ズーム・パン（周波数/dB 軸それぞれ独立）と、スナップショット・ターゲット・予測カーブの
// 重ね描きに対応（仕様 5.2）。
// 60fpsアニメーションタイマーでヌルヌル描画（ライブデータ時のパルス表示等）。
// 表示モード切替・縦スケールプリセット・コヒーレンスブランキング対応（issue #144）。
class FrequencyGraphView : public juce::Component,
                           public juce::Timer
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

        // Issue #143: スナップショットごとの表示調整
        double dbOffset = 0.0;      // dB オフセット（垂直シフト）
        double delayOffset = 0.0;   // 遅延オフセット（サンプル）
        int smoothingOverride = -1; // -1 = グローバル設定使用、0 = なし、3 = 1/3 oct 等
    };

    /** 30Hz程度のタイマーから呼ぶ。smoothingOctDenom: 0=なし、3=1/3 oct、48=1/48 oct。
        appliedDelaySamples: 位相表示の遅延補正値（0なら生位相）。showRawPhase: 補正を無視して生位相 */
    void setLiveFrame (const eeq::dsp::SpectrumFrame& frame, int smoothingOctDenom,
                       double appliedDelaySamples, bool showRawPhase);

    /** ワーカーの事前計算結果を共有し、新しい点列が届いたときだけ描画列へ変換する */
    void setLiveFrameCached (std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> points,
                             std::shared_ptr<const eeq::dsp::SpectrumFrame> frame,
                             int smoothingOctDenom,
                             double appliedDelaySamples, bool showRawPhase);

    /** plotArea の幅を返す（キャッシュ計算用） */
    int getPlotWidthForCache() const noexcept { return juce::jmax (2, (int) plotArea().getWidth()); }
    double getViewMinForCache() const noexcept { return axis.viewMin; }
    double getViewMaxForCache() const noexcept { return axis.viewMax; }

    /** オーバーレイを差し替える（version が変わったときだけ再計算する） */
    void setOverlays (const std::vector<OverlaySource>& sources, int version, int smoothingOctDenom);

    /** 表示モード設定（issue #144） */
    void setViewMode (ViewMode mode);
    ViewMode getViewMode() const noexcept { return viewMode; }

    /** 縦スケールプリセット設定（issue #144） */
    void setScalePreset (ScalePreset preset);
    ScalePreset getScalePreset() const noexcept { return scalePreset; }

    /** コヒーレンスブランキング有効/無効（issue #144） */
    void setCoherenceBlanking (bool enabled);
    bool getCoherenceBlanking() const noexcept { return coherenceBlankingEnabled; }

public:
    /** オーバーレイの平滑化済み列（バックグラウンドワーカーからの結果用） */
    struct OverlayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
    };

    /** オーバーレイ平滑化済みの列を直接設定する（issue #66: バックグラウンドワーカーからの結果） */
    void setOverlayColumns (const std::vector<std::vector<OverlayColumn>>& columns, int version);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Timer interface for 60fps smooth animation
    void timerCallback() override;

    /** スナップショット等の平滑化に使うグリッド幅（タブ側の差分計算で揃える） */
    [[nodiscard]] int plotWidth() const noexcept { return juce::jmax (2, (int) plotArea().getWidth()); }

    // Issue #145: マーカー管理
    struct Marker
    {
        double freq = 0.0;
        float ampDb = 0.0f;
        float phaseDeg = 0.0f;
        float coherence = 0.0f;
        juce::Colour colour;
        juce::String label;
    };
    void addMarker (double freq, const juce::String& label = {});
    void removeMarker (int index);
    void clearMarkers();
    const std::vector<Marker>& getMarkers() const noexcept { return markers; }
    void setCursorFrequency (double freq); // プログラムからカーソル位置を設定

private:
    struct DisplayColumn
    {
        double freq = 0.0;
        std::complex<float> h;
        float coherence = 0.0f;
    };

    void rebuildLive();
    void rebuildOverlays();
    const eeq::dsp::SpectrumFrame& currentFrame() const noexcept
    { return currentFrameShared != nullptr ? *currentFrameShared : current; }

    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> amplitudeArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> lowerArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> phaseArea (const juce::Rectangle<float>& plot) const;
    juce::Rectangle<float> coherenceArea (const juce::Rectangle<float>& plot) const;

    LogFrequencyAxis axis;

    eeq::dsp::SpectrumFrame current;
    std::shared_ptr<const eeq::dsp::SpectrumFrame> currentFrameShared;
    std::shared_ptr<const std::vector<eeq::dsp::GridPoint>> cachedLivePoints;
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
    bool livePointsAreCached = false; // ライブ曲線をタブ側timerのキャッシュから受け取る
    bool dataDirty = true;   // 直接計算モードで測定データ変更時に smoothToLogGrid 再計算
    bool viewDirty = false;  // ズーム/パンで再描画のみ必要
    int hoverColumn = -1;

    // ドラッグ開始位置（パン用）
    juce::Point<float> dragStart;
    float dragStartAmpTop = 0.0f;

    // 振幅軸。auto のときは滑らかに追従、固定のときはホイール/ドラッグで調整（仕様 5.5）
    bool ampAuto = true;
    bool ampScaleConverging = false;
    float ampTop = 10.0f;
    float ampSpan = 100.0f;

    // 表示モード・縦スケールプリセット・コヒーレンスブランキング（issue #144）
    ViewMode viewMode = ViewMode::ampPhaseCoh;
    ScalePreset scalePreset = ScalePreset::autoScale;
    bool coherenceBlankingEnabled = true;
    bool scaleLocked = false; // スケール固定フラグ

    static constexpr double freqLimitMin = 20.0;
    static constexpr double freqLimitMax = 20000.0;
    static constexpr float coherenceThreshold = 0.8f; // 仕様 4.1 の既定しきい値
    static constexpr float dimmedAlpha = 0.25f;
    static constexpr int dBLabelStripWidth = 40;

    // 60fps なめらかアニメーション用
    double animationPhase = 0.0;
    bool hasLiveData = false; // ライブデータがあるか（パルス表示用）

    // Issue #145: カーソル・マーカー
    std::vector<Marker> markers;
    int nextMarkerColourIndex = 0;
    static juce::Colour getMarkerColour (int index);
    double forcedCursorFreq = -1.0; // プログラム指定のカーソル周波数

    void startAnimationTimer();
    void applyScalePreset();
    void drawMarkers (juce::Graphics&, const juce::Rectangle<float>& ampArea,
                      const juce::Rectangle<float>& phaseArea);
    void drawMarkerDeltas (juce::Graphics&, const juce::Rectangle<float>& ampArea);
    void drawCursorReadout (juce::Graphics&, const juce::Rectangle<float>& ampArea);
};

} // namespace eeq::ui
