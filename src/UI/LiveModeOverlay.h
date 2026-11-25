#pragma once

#include <JuceHeader.h>

#include "../Audio/AudioEngine.h"
#include "../Core/KeepAwake.h"
#include "../DSP/Waterfall.h"
#include "Theme.h"

#include <atomic>
#include <deque>

namespace eeq::ui
{

// 本番モード（仕様 5.4）。ワンボタンで切り替わる専用画面。
// SPL（Fast/Slow切替）を大きく、Leq（1分・15分・開始から）、ピーク、直近60分の推移、
// 1/3 oct RTA を下段に常時表示。しきい値で緑→黄→赤に色分けし、超過は枠色でも知らせる。
// ウォーターフォール表示（5.4.1）に切替可能: 持続する狭帯域のピーク（ハウリングの
// 始まりなど）が帯として見える。
// 誤操作防止のため、戻る操作は長押しで行う。SPL と Leq を1秒ごとに CSV へ自動保存。
class LiveModeOverlay : public juce::Component
{
public:
    explicit LiveModeOverlay (eeq::audio::AudioEngine& engineIn);

    /** 30Hzタイマーから呼ぶ（MainComponent側で統一して呼ぶ） */
    void timerTick();

    /** 本番モードの開始/終了。終了時は必ず呼ぶ（ログ停止・スリープ防止解除） */
    void beginLiveMode();
    void endLiveMode();

    /** 長押しで戻る操作が確定したときに呼ばれる（MainComponentがオーバーレイを閉じる） */
    std::function<void()> onHideRequested;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // 長押し専用ボタン（誤操作防止: 仕様 5.4）
    class LongPressButton : public juce::TextButton,
                            private juce::Timer
    {
    public:
        std::function<void()> onLongPress;
        int pressMs = 800;

        void mouseDown (const juce::MouseEvent&) override
        {
            pressStart = juce::Time::getMillisecondCounter();
            startTimerHz (10);
        }

        void mouseUp (const juce::MouseEvent&) override { stopTimer(); }

        void timerCallback() override
        {
            if (! isMouseButtonDown())
            {
                stopTimer();
                return;
            }

            if (juce::Time::getMillisecondCounter() - pressStart >= (uint32) pressMs)
            {
                stopTimer();

                if (onLongPress != nullptr)
                    onLongPress();
            }
        }

    private:
        uint32 pressStart = 0;
    };

    void paintHistory (juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour frameColour);
    void paintWaterfall (juce::Graphics& g, const juce::Rectangle<float>& area);
    void paintRta (juce::Graphics& g, const juce::Rectangle<float>& area);

    eeq::audio::AudioEngine& engine;

    LongPressButton exitButton;
    juce::TextButton waterfallButton;
    juce::Label splLabel, leqLabel, peakLabel, sessionLabel, hintLabel, historyTitle;

    // 直近60分のSPL推移（1秒間隔）
    std::deque<float> historySpl;
    juce::Time lastLogTime;
    juce::Time lastWaterfallPush;
    std::unique_ptr<eeq::core::KeepAwake> keepAwake;

    // ウォーターフォール（仕様 5.4.1）: 1/3 oct の帯域レベルを0.5秒間隔で溜める
    eeq::dsp::Waterfall waterfall;
    bool waterfallVisible = false;

    // CSVログ（仕様 5.4）
    juce::File currentLogFile;
    bool loggingActive = false;

    // RTA表示のキャッシュ
    std::vector<double> rtaCenters;
    std::vector<float> rtaLevels;

    static constexpr int greenThresholdDb = 95;  // しきい値（規制値に合わせて変更可）
    static constexpr int yellowThresholdDb = 105;
    static constexpr int historySeconds = 60 * 60;
    static constexpr int waterfallSeconds = 300; // 表示長 5分（30秒〜10分の範囲内）
};

} // namespace eeq::ui
