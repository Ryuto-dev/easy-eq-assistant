#include "LiveModeOverlay.h"

#include "BottomBar.h"

#include "../Core/Exporters.h"
#include "../Core/Log.h"
#include "Buttons.h"
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

// ウォーターフォールのレベル→色（青→シアン→黄→白の簡易ヒートカラー）
juce::Colour waterfallColour (float normalized)
{
    normalized = juce::jlimit (0.0f, 1.0f, normalized);

    if (normalized < 0.5f)
    {
        const auto t = normalized / 0.5f;
        return juce::Colour::fromHSV (0.6f - 0.1f * t, 0.85f, 0.12f + 0.5f * t, 1.0f);
    }

    const auto t = (normalized - 0.5f) / 0.5f;
    return juce::Colour::fromHSV (0.1f - 0.1f * t, 0.85f - 0.6f * t, 0.62f + 0.38f * t, 1.0f);
}
} // namespace

LiveModeOverlay::LiveModeOverlay (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn)
{
    setOpaque (true);

    exitButton.setButtonText (strings::liveExitButton);
    styleDangerButton (exitButton);
    exitButton.onLongPress = [this]
    {
        if (onHideRequested != nullptr)
            onHideRequested();
    };
    addAndMakeVisible (exitButton);

    // ウォーターフォール切替（仕様 5.4.1） - デフォルトON（垂直表示・RTAと帯域合わせ）
    styleButton (waterfallButton);
    waterfallButton.setButtonText (strings::liveWaterfallToggle);
    waterfallButton.onClick = [this]
    {
        waterfallVisible = ! waterfallVisible;

        if (waterfallVisible)
        {
            // RTAと同じ帯域分解能（1/3オクターブ）で垂直ウォーターフォール
            waterfall.configure (3, (double) waterfallSeconds, -100.0, -40.0);
            waterfall.clear();
            waterfallImage = juce::Image();
            lastWaterfallPush = {};
        }
    };
    waterfallVisible = true; // デフォルトON
    waterfall.configure (3, (double) waterfallSeconds, -100.0, -40.0);
    addAndMakeVisible (waterfallButton);

    for (auto* label : { &splLabel, &leqLabel, &peakLabel, &sessionLabel })
        addAndMakeVisible (label);

    // 履歴グラフのタイトルは paintHistory が描くため、専用ラベルは持たない

    hintLabel.setText (strings::liveLongPressHint, juce::dontSendNotification);

    // 警告しきい値（注意/上限）。規制値や会場ごとに変える（issue #149）
    for (auto* title : { &warnThresholdTitle, &limitThresholdTitle })
    {
        styleLabel (*title);
        addAndMakeVisible (title);
    }

    warnThresholdTitle.setText (strings::liveWarnThreshold, juce::dontSendNotification);
    limitThresholdTitle.setText (strings::liveLimitThreshold, juce::dontSendNotification);

    for (auto* combo : { &warnThresholdCombo, &limitThresholdCombo })
    {
        styleCombo (*combo);

        for (int db = 80; db <= 120; ++db)
            combo->addItem (juce::String (db) + " dB", db);

        combo->onChange = [this] { applyThresholdsFromCombos(); };
        addAndMakeVisible (combo);
    }

    warnThresholdCombo.setSelectedId (greenThresholdDb, juce::dontSendNotification);
    limitThresholdCombo.setSelectedId (yellowThresholdDb, juce::dontSendNotification);

    // 発振器の出力中バッジ（issue #141）: 本番モードでは下部バーが見えないため、ここにも出す
    outputBadge.setColour (juce::Label::backgroundColourId, theme::get (theme::ColourId::danger));
    outputBadge.setColour (juce::Label::textColourId, juce::Colours::white);
    outputBadge.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    outputBadge.setJustificationType (juce::Justification::centred);
    addChildComponent (outputBadge);
    hintLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (hintLabel);
}

void LiveModeOverlay::setSettings (juce::PropertiesFile* settingsToUse)
{
    settings = settingsToUse;

    if (settings == nullptr)
        return;

    warnThresholdCombo.setSelectedId (juce::jlimit (80, 120, settings->getIntValue ("liveWarnThresholdDb", 95)),
                                      juce::dontSendNotification);
    limitThresholdCombo.setSelectedId (juce::jlimit (80, 120, settings->getIntValue ("liveLimitThresholdDb", 105)),
                                       juce::dontSendNotification);
    applyThresholdsFromCombos();
}

void LiveModeOverlay::applyThresholdsFromCombos()
{
    greenThresholdDb = warnThresholdCombo.getSelectedId();
    yellowThresholdDb = juce::jmax (greenThresholdDb + 1, limitThresholdCombo.getSelectedId());

    if (limitThresholdCombo.getSelectedId() != yellowThresholdDb)
        limitThresholdCombo.setSelectedId (juce::jmin (120, yellowThresholdDb), juce::dontSendNotification);

    if (settings != nullptr)
    {
        settings->setValue ("liveWarnThresholdDb", greenThresholdDb);
        settings->setValue ("liveLimitThresholdDb", yellowThresholdDb);
    }

    repaint();
}

void LiveModeOverlay::beginLiveMode()
{
    setVisible (true);
    keepAwake = std::make_unique<eeq::core::KeepAwake>(); // 画面スリープ防止（仕様 5.4）

    // SPLログ: 1秒ごとにCSVへ自動保存（仕様 5.4）
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("EasyEQAssistant")
                         .getChildFile ("spl");

    if (! dir.exists())
        dir.createDirectory();

    currentLogFile = dir.getChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".csv");
    loggingActive = eeq::core::exporters::createSplLogCsv (currentLogFile);
    eeq::log::write (loggingActive ? "live mode: SPL log started: " + currentLogFile.getFullPathName()
                                   : "live mode: SPL log could not be created");

    lastLogTime = juce::Time::getCurrentTime();
    lastWaterfallPush = lastLogTime;
    historySpl.clear();

    if (waterfallVisible)
        waterfall.clear();
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

    // 校正済みなら「未校正」注記を出さない（issue #139）
    {
        const auto calibrated = std::abs (engine.getSplAnalyzer().getCalibrationOffsetDb()) > 1.0e-9;
        auto hint = strings::liveLongPressHint;

        if (! calibrated)
            hint << " / " << strings::splUncalibrated;

        if (! loggingActive)
            hint << " / " << strings::liveLogInactive;

        if (hintLabel.getText() != hint)
            hintLabel.setText (hint, juce::dontSendNotification);
    }

    // 出力中バッジ（issue #141）
    {
        const auto audible = engine.isNoiseAudible();
        outputBadge.setVisible (audible);

        if (audible)
            outputBadge.setText (BottomBar::outputStatusText (engine), juce::dontSendNotification);
    }

    // 1秒ごとの履歴とCSV保存（仕様 5.4）。履歴はログの成否と切り離す（issue #149: ログが失敗しても止めない）
    const auto now = juce::Time::getCurrentTime();

    if ((now.toMilliseconds() - lastLogTime.toMilliseconds()) >= 1000)
    {
        lastLogTime = now;

        if (spl.hasSignal)
        {
            historySpl.push_back ((float) spl.currentDb);

            while ((int) historySpl.size() > historySeconds)
                historySpl.pop_front();

            // 書き込みが続けられない（移動/ディスク満杯など）場合は通知して止める
            if (loggingActive
                && ! eeq::core::exporters::appendSplLogLine (currentLogFile, now.formatted ("%H:%M:%S"),
                                                             spl.currentDb, spl.leq1minDb, spl.leq15minDb,
                                                             spl.leqSessionDb, spl.peakDb))
            {
                loggingActive = false;
                eeq::log::write ("live mode: SPL log write FAILED, logging stopped");
            }
        }
    }

    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    engine.getSplAnalyzer().copyRta (frame);
    rtaCenters = frame.centerFreq;
    rtaLevels = frame.levelDb;

    // ウォーターフォールへ0.5秒間隔で帯域レベルを積む（仕様 5.4.1）。
    // 色の dB レンジは現在のピークに追従させる。
    // 描画はオフスクリーン画像に積み上げるため、ここでの再描画は1回のdrawImageで済む（issue #24）
    if (waterfallVisible && (now.toMilliseconds() - lastWaterfallPush.toMilliseconds()) >= 500)
    {
        lastWaterfallPush = now;
        pushWaterfallColumn();
    }

    repaint();
}

juce::Rectangle<float> LiveModeOverlay::graphArea() const
{
    auto inset = getLocalBounds().toFloat().reduced (16.0f);
    inset.removeFromBottom (110.0f); // RTA（paint と同じ）

    return inset.removeFromBottom (170.0f);
}

void LiveModeOverlay::pushWaterfallColumn()
{
    const auto area = graphArea();
    const auto imgW = juce::jmax (2, (int) area.getWidth());
    const auto imgH = juce::jmax (2, (int) area.getHeight());

    if (! waterfallImage.isValid() || waterfallImage.getWidth() != imgW
        || waterfallImage.getHeight() != imgH)
    {
        waterfallImage = juce::Image (juce::Image::ARGB, imgW, imgH, true);
    }

    float peakLevel = -300.0f;

    for (const auto level : rtaLevels)
        peakLevel = juce::jmax (peakLevel, level);

    if (peakLevel > -200.0f)
        waterfall.setDbRange (peakLevel - 55.0, peakLevel - 5.0);

    waterfall.push (rtaLevels, 0.5);

    const auto bands = waterfall.numBands();
    const auto rows = waterfall.numRows();

    if (bands < 1 || rows < 1)
        return;

    // 垂直ウォーターフォール: 時間は縦軸（下が新しい）、周波数は横軸（RTAと合わせる）
    // 既存画像を1行分上へずらし、最新行を下端に描く
    juce::Image shifted (juce::Image::ARGB, imgW, imgH, true);
    {
        juce::Graphics gs (shifted);
        const auto bands2 = waterfall.numBands();
        const auto colW = (float) imgW / (float) juce::jmax (1, bands2); // 周波数=横
        const auto rowH = (float) imgH / (float) juce::jmax (1, waterfallSeconds * 2); // 時間=縦
        const auto shiftPy = (int) juce::jmax (1.0f, rowH);

        // 既存を上へシフト
        gs.drawImageAt (waterfallImage, 0, -shiftPy);

        // 最新行（rows-1）を下端に描く
        const auto& levels = waterfall.row (rows - 1);
        const auto lo = (float) waterfall.getMinDb();
        const auto hi = (float) waterfall.getMaxDb();

        for (int b = 0; b < bands2 && b < (int) levels.size(); ++b)
        {
            if (levels[(size_t) b] < -200.0f)
                continue;

            const auto normalized = juce::jlimit (0.0f, 1.0f, (levels[(size_t) b] - lo) / (hi - lo));
            gs.setColour (waterfallColour (normalized));
            gs.fillRect ((float) b * colW, (float) (imgH - shiftPy), juce::jmax (1.0f, colW), (float) juce::jmax (1, shiftPy));
        }
    }

    waterfallImage = std::move (shifted);
}

void LiveModeOverlay::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

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

    // ---- 下段: RTA（常時表示。仕様 5.4）----
    const auto rtaHeight = 110.0f;
    const auto rtaArea = inset.removeFromBottom (rtaHeight);
    paintRta (g, rtaArea);

    // ---- 中段: SPL推移 or ウォーターフォール（仕様 5.4 / 5.4.1）----
    const auto graphHeight = 170.0f;
    const auto graphArea = inset.removeFromBottom (graphHeight);

    if (waterfallVisible)
        paintWaterfall (g, graphArea);
    else
        paintHistory (g, graphArea, frameColour);

    juce::ignoreUnused (bounds);
}

void LiveModeOverlay::paintHistory (juce::Graphics& g, const juce::Rectangle<float>& area,
                                    juce::Colour frameColour)
{
    g.setColour (theme::get (theme::ColourId::grid));
    g.drawRect (area, 1.0f);

    if (! historySpl.empty())
    {
        juce::Path path;
        const auto n = (int) historySpl.size();

        for (int i = 0; i < n; ++i)
        {
            // 右端が「今」。60分幅に対して履歴の位置を打つ
            const auto x = area.getX()
                         + (float) (historySeconds - n + i) / (float) (historySeconds - 1) * area.getWidth();
            const auto db = juce::jlimit (40.0f, 130.0f, historySpl[(size_t) i]);
            const auto y = area.getBottom() - (db - 40.0f) / 90.0f * area.getHeight();

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        g.setColour (frameColour);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (strings::liveSplHistory, (int) area.getX() + 4, (int) area.getY() - 16, 200, 14,
                juce::Justification::centredLeft);
}

void LiveModeOverlay::paintWaterfall (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    g.setColour (theme::get (theme::ColourId::grid));
    g.drawRect (area, 1.0f);

    // 垂直ウォーターフォール: 時間は縦軸（下が現在）、周波数は横軸（RTAと合わせる）
    if (waterfallImage.isValid())
        g.drawImageAt (waterfallImage, (int) area.getX(), (int) area.getY());

    // 周波数ラベル（RTAと合わせる）
    if (! rtaCenters.empty())
    {
        g.setColour (theme::get (theme::ColourId::gridText));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));

        const auto count = (int) rtaCenters.size();
        const auto barWidth = area.getWidth() / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const auto f = rtaCenters[(size_t) i];

            if (f < 30.0 || f > 16000.0)
                continue;

            // 1/3オクターブの標準中心だけラベル
            const auto stepsFrom20 = std::log2 (f / 20.0) * 3.0;

            if (std::abs (stepsFrom20 - std::round (stepsFrom20)) < 0.05)
            {
                const auto text = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
                const auto x = area.getX() + ((float) i + 0.5f) * barWidth;
                g.drawText (text, (int) x - 20, (int) area.getBottom() + 2, 40, 12,
                            juce::Justification::centred);
            }
        }
    }

    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (strings::liveWaterfallToggle + juce::String (u8"（下端が現在）"), (int) area.getX() + 4,
                (int) area.getY() - 16, 300, 14, juce::Justification::centredLeft);
}
void LiveModeOverlay::paintRta (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    if (rtaCenters.empty() || area.getWidth() < 50.0f)
        return;

    const auto plot = area.withTrimmedBottom (14.0f);
    const auto count = (int) rtaCenters.size();
    const auto barWidth = plot.getWidth() / (float) count;

    // 表示レンジはフレーム内のピークから60 dB下まで（校正オフセットに影響されない）
    float peakLevel = -300.0f;

    for (const auto level : rtaLevels)
        peakLevel = juce::jmax (peakLevel, level);

    const auto rangeTop = peakLevel > -200.0f ? peakLevel : 0.0f;
    const auto rangeBottom = rangeTop - 60.0f;

    bool anySignal = false;
    for (int i = 0; i < count; ++i)
    {
        const auto db = rtaLevels[(size_t) i];

        if (db < -200.0f)
            continue;

        anySignal = true;
        const auto normalized = juce::jlimit (0.0f, 1.0f, (db - rangeBottom) / 60.0f);
        const auto h = normalized * plot.getHeight();
        const auto x = plot.getX() + (float) i * barWidth;

        g.setColour (theme::get (theme::ColourId::accent).withAlpha (0.65f));
        g.fillRect (x + 1.0f, plot.getBottom() - h, barWidth - 2.0f, h);
    }

    // 無音時の表示（OS音量0やマイク未接続等で入力がない場合）
    if (! anySignal)
    {
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (strings::rtaNoSignal, plot, juce::Justification::centred);
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
            const auto x = plot.getX() + ((float) i + 0.5f) * barWidth;
            g.drawText (text, (int) x - 20, (int) plot.getBottom() + 2, 40, 12,
                        juce::Justification::centred);
        }
    }

    // dB目盛り（issue #27）: 20 dB 刻みの水平グリッドと値。範囲はピーク-60 dB
    g.setFont (juce::Font (juce::FontOptions (9.0f)));

    for (auto db = std::ceil (rangeBottom / 20.0f) * 20.0f; db <= rangeTop; db += 20.0f)
    {
        const auto y = plot.getBottom() - (db - rangeBottom) / 60.0f * plot.getHeight();

        if (y < plot.getY() - 0.5f || y > plot.getBottom() + 0.5f)
            continue;

        g.setColour (theme::get (theme::ColourId::grid).withAlpha (0.45f));
        g.drawLine (plot.getX(), y, plot.getRight(), y);

        g.setColour (theme::get (theme::ColourId::gridText));
        g.drawText (juce::String (db, 0), (int) plot.getX() + 2, (int) y - 6, 40, 12,
                    juce::Justification::centredLeft);
    }
}

void LiveModeOverlay::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    auto topRow = bounds.removeFromTop (34);
    exitButton.setBounds (topRow.removeFromLeft (160));
    topRow.removeFromLeft (8);
    waterfallButton.setBounds (topRow.removeFromLeft (140));
    topRow.removeFromLeft (16);
    warnThresholdTitle.setBounds (topRow.removeFromLeft (44));
    warnThresholdCombo.setBounds (topRow.removeFromLeft (90).reduced (0, 4));
    topRow.removeFromLeft (8);
    limitThresholdTitle.setBounds (topRow.removeFromLeft (44));
    limitThresholdCombo.setBounds (topRow.removeFromLeft (90).reduced (0, 4));
    topRow.removeFromLeft (16);
    outputBadge.setBounds (topRow.removeFromRight (juce::jmin (360, topRow.getWidth())));

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

    // 下段（RTA+グラフ = 280px）は paint が使う。ラベルが重ならないよう余白を確保
    juce::ignoreUnused (content);
}

} // namespace eeq::ui
