#include "LiveModeOverlay.h"

#include "../Core/Exporters.h"
#include "../Core/Log.h"
#include "Strings.h"

namespace eeq::ui
{

namespace
{
// SPL値を緑→黄→赤に色分けする（仕様 5.4）
juce::Colour splColour (double spl, int greenThreshold, int yellowThreshold)
{
    if (spl >= yellowThreshold)
        return theme::get (theme::ColourId::danger);

    if (spl >= greenThreshold)
        return theme::get (theme::ColourId::warn);

    return theme::get (theme::ColourId::ok);
}
} // namespace

LiveModeOverlay::LiveModeOverlay (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn)
{
    setOpaque (true);

    exitButton.setButtonText (strings::liveExitButton);
    exitButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8c3a3a));
    exitButton.setColour (juce::TextButton::textColourOffId, theme::get (theme::ColourId::text));
    exitButton.onLongPress = [this]
    {
        if (onHideRequested != nullptr)
            onHideRequested();
    };
    addAndMakeVisible (exitButton);

    for (auto* label : { &splLabel, &leqLabel, &peakLabel, &sessionLabel, &historyTitle })
        addAndMakeVisible (label);

    historyTitle.setText (strings::liveSplHistory, juce::dontSendNotification);
    historyTitle.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));

    hintLabel.setText (strings::liveLongPressHint + " / " + strings::splUncalibrated,
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (hintLabel);
}

void LiveModeOverlay::beginLiveMode()
{
    setVisible (true);
    keepAwake = std::make_unique<eeq::core::KeepAwake>(); // 画面スリープ防止（仕様 5.4）

    // SPLログ: 1秒ごとにCSVへ自動保存（仕様 5.4）
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("EasyEQAssistant")
                         .getChildFile ("spl");

    currentLogFile = dir.getChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".csv");
    loggingActive = eeq::core::exporters::createSplLogCsv (currentLogFile);
    eeq::log::write (loggingActive ? "live mode: SPL log started: " + currentLogFile.getFullPathName()
                                   : "live mode: SPL log could not be created");

    lastLogTime = juce::Time::getCurrentTime();
    historySpl.clear();
}

void LiveModeOverlay::endLiveMode()
{
    setVisible (false);
    keepAwake = nullptr;

    if (loggingActive)
    {
        loggingActive = false;
        eeq::log::write ("live mode: SPL log stopped: " + currentLogFile.getFullPathName());
    }
}

void LiveModeOverlay::timerTick()
{
    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    engine.getSplAnalyzer().copySpl (spl);

    splLabel.setText (spl.hasSignal ? juce::String (spl.currentDb, 1) : "--",
                      juce::dontSendNotification);
    splLabel.setColour (juce::Label::textColourId,
                        splColour (spl.currentDb, greenThresholdDb, yellowThresholdDb));

    leqLabel.setText (juce::String (u8"Leq   ") + strings::splLeq1min + ": "
                          + juce::String (spl.leq1minDb, 1) + "   " + strings::splLeq15min + ": "
                          + juce::String (spl.leq15minDb, 1) + "   " + strings::splLeqSession + ": "
                          + juce::String (spl.leqSessionDb, 1),
                      juce::dontSendNotification);

    peakLabel.setText (strings::splPeakLabel + ": " + juce::String (spl.peakDb, 1) + " dB",
                       juce::dontSendNotification);

    sessionLabel.setText (juce::String ((int) (spl.sessionSeconds / 60.0)) + " min",
                          juce::dontSendNotification);

    // 1秒ごとのCSV保存と履歴（仕様 5.4）
    const auto now = juce::Time::getCurrentTime();

    if (loggingActive && (now.toMilliseconds() - lastLogTime.toMilliseconds()) >= 1000)
    {
        lastLogTime = now;

        if (spl.hasSignal)
        {
            historySpl.push_back ((float) spl.currentDb);

            while ((int) historySpl.size() > historySeconds)
                historySpl.pop_front();

            eeq::core::exporters::appendSplLogLine (currentLogFile, now.formatted ("%H:%M:%S"),
                                                    spl.currentDb, spl.leq1minDb, spl.leq15minDb,
                                                    spl.leqSessionDb, spl.peakDb);
        }
    }

    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    engine.getSplAnalyzer().copyRta (frame);
    rtaCenters = frame.centerFreq;
    rtaLevels = frame.levelDb;

    repaint();
}

void LiveModeOverlay::timerCallback() {}

void LiveModeOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0c0d10));

    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    engine.getSplAnalyzer().copySpl (spl);

    const auto frameColour = splColour (spl.currentDb, greenThresholdDb, yellowThresholdDb);

    // しきい値超過は画面全体の枠色でも知らせる（仕様 5.4）
    if (spl.hasSignal && spl.currentDb >= greenThresholdDb)
    {
        g.setColour (frameColour.withAlpha (0.9f));
        g.drawRect (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
    }

    const auto bounds = getLocalBounds().toFloat();
    auto inset = bounds.reduced (16.0f);

    // ---- RTA（下段に常時表示。仕様 5.4）----
    const auto rtaHeight = 120.0f;
    const auto rtaArea = inset.removeFromBottom (rtaHeight);

    if (! rtaCenters.empty() && rtaArea.getWidth() > 50.0f)
    {
        const auto count = (int) rtaCenters.size();
        const auto barWidth = rtaArea.getWidth() / (float) count;

        // 表示レンジはフレーム内のピークから60 dB下まで（校正オフセットに影響されない）
        float peakLevel = -300.0f;

        for (const auto level : rtaLevels)
            peakLevel = juce::jmax (peakLevel, level);

        const auto rangeTop = peakLevel > -200.0f ? peakLevel : 0.0f;
        const auto rangeBottom = rangeTop - 60.0f;

        for (int i = 0; i < count; ++i)
        {
            const auto db = rtaLevels[(size_t) i];

            if (db < -200.0f)
                continue;

            const auto normalized = juce::jlimit (0.0f, 1.0f, (db - rangeBottom) / 60.0f);
            const auto h = normalized * (rtaArea.getHeight() - 16.0f);
            const auto x = rtaArea.getX() + (float) i * barWidth;

            g.setColour (theme::get (theme::ColourId::accent).withAlpha (0.65f));
            g.fillRect (x + 1.0f, rtaArea.getBottom() - 14.0f - h, barWidth - 2.0f, h);
        }

        g.setColour (theme::get (theme::ColourId::gridText));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));

        for (int i = 0; i < count; ++i)
        {
            const auto f = rtaCenters[(size_t) i];

            if (f < 30.0 || f > 16000.0)
                continue;

            // 1/3オクターブの標準中心（幾何系列が整数段に乗るところ）だけラベルを打つ
            const auto stepsFrom20 = std::log2 (f / 20.0) * 3.0;

            if (std::abs (stepsFrom20 - std::round (stepsFrom20)) < 0.05)
            {
                const auto text = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
                const auto x = rtaArea.getX() + ((float) i + 0.5f) * barWidth;
                g.drawText (text, (int) x - 20, (int) rtaArea.getBottom() - 14, 40, 12,
                            juce::Justification::centred);
            }
        }
    }

    // ---- SPL推移（直近60分。仕様 5.4）----
    const auto historyHeight = 90.0f;
    const auto historyArea = inset.removeFromBottom (historyHeight);

    g.setColour (theme::get (theme::ColourId::grid));
    g.drawRect (historyArea, 1.0f);

    if (! historySpl.empty())
    {
        juce::Path path;
        const auto n = (int) historySpl.size();

        for (int i = 0; i < n; ++i)
        {
            // 右端が「今」。60分幅に対して履歴の位置を打つ
            const auto x = historyArea.getX()
                         + (float) (historySeconds - n + i) / (float) (historySeconds - 1) * historyArea.getWidth();
            const auto db = juce::jlimit (40.0f, 130.0f, historySpl[(size_t) i]);
            const auto y = historyArea.getBottom() - (db - 40.0f) / 90.0f * historyArea.getHeight();

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        g.setColour (frameColour);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    // ---- 大きいSPL表示の土台はLabelに任せる（resized で配置）----
    juce::ignoreUnused (bounds);
}

void LiveModeOverlay::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    exitButton.setBounds (bounds.removeFromTop (34).removeFromLeft (160));

    auto content = bounds.reduced (8, 4);

    // SPL を画面幅いっぱいの大きな数字で（仕様 5.4）
    const auto splHeight = (int) (content.getHeight() * 0.5f);
    splLabel.setBounds (content.removeFromTop (splHeight));
    splLabel.setFont (juce::Font (juce::FontOptions (splHeight * 0.72f, juce::Font::bold)));
    splLabel.setJustificationType (juce::Justification::centred);

    leqLabel.setBounds (content.removeFromTop (26));
    leqLabel.setFont (juce::Font (juce::FontOptions (18.0f)));
    leqLabel.setJustificationType (juce::Justification::centred);

    auto bottomRow = content.removeFromTop (24);
    peakLabel.setBounds (bottomRow.removeFromRight (bottomRow.getWidth() / 2));
    peakLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
    peakLabel.setJustificationType (juce::Justification::centredRight);
    sessionLabel.setBounds (bottomRow);
    sessionLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
    sessionLabel.setJustificationType (juce::Justification::centredLeft);

    hintLabel.setBounds (content.removeFromTop (18));
    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    hintLabel.setJustificationType (juce::Justification::centred);

    // RTAと履歴の上に余白を残す（paint が下段 210px を使う）
    juce::ignoreUnused (content);
}

} // namespace eeq::ui
