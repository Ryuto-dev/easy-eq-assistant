#pragma once

#include "Core/JuceModules.h"

#include "../Core/SpectrumLog.h"
#include "UiContext.h"

namespace eeq::ui
{

// 「ログ」ページ（2026-11）。本番モード中に記録した周波数ログを開いて見返す。
//   上: SPL の推移（細線 = 1 秒の値、太線 = Leq 1 分）
//   中: 時間 × 周波数の色の図（スペクトログラム）
//   下: 帯域バランス（低域 / 中域 / 高域が、その本番の平均より何 dB 多いか。10 秒で平滑化）
//   右: カーソル位置のスペクトル（本番の平均との比較）
// マウスホイールで時間を拡大、ダブルクリックで全体に戻す。記録中のファイルは 5 秒ごとに読み直す。
class LogViewerTab : public juce::Component
{
public:
    explicit LogViewerTab (UiContext& context);

    void timerTick();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::File logDirectory();

private:
    void refreshFileList();
    void loadSelected (bool keepView);
    void rebuildDerived();
    int rowAtX (float x) const;
    float xForSeconds (double s) const;

    void drawSpl (juce::Graphics&, juce::Rectangle<float> area);
    void drawSpectrogram (juce::Graphics&, juce::Rectangle<float> area);
    void drawBalance (juce::Graphics&, juce::Rectangle<float> area);
    void drawTimeAxis (juce::Graphics&, juce::Rectangle<float> plot, float y);
    void drawSidePanel (juce::Graphics&, juce::Rectangle<int> area);

    UiContext& ctx;
    juce::Label titleLabel, infoLabel;
    juce::ComboBox fileCombo;
    juce::TextButton reloadButton, folderButton;
    juce::Array<juce::File> files;

    eeq::core::SpectrumLog log;
    juce::File loadedFile;
    juce::Time loadedModified;
    std::vector<float> averageSpectrum;            // 本番全体の平均（dB）
    std::vector<float> lowDelta, midDelta, highDelta; // 帯域バランス（平均比 dB、10 秒平滑）
    juce::Image spectrogram;
    float specMinDb = 40.0f, specMaxDb = 100.0f;

    double viewStart = 0.0, viewEnd = 1.0;  // 表示している経過秒
    int cursorRow = -1;                     // ホバー中
    int pinnedRow = -1;                     // クリックで固定
    float dragStartX = 0.0f;
    double dragStartView = 0.0;

    std::vector<juce::Rectangle<int>> cardRects;
    juce::Rectangle<int> plotCard, sideCard;
    juce::Rectangle<float> splArea, specArea, balanceArea; // 時間軸を共有するプロット
    juce::uint32 lastPollMs = 0;
};

} // namespace eeq::ui
