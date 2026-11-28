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

// 帯域の棒の下にオクターブごとの周波数を打つ（帯域の細かさ 1/1〜1/24 oct に関係なく、
// 31.5 / 63 / 125 … 16k に最も近い帯域の位置へ）
void drawOctaveLabels (juce::Graphics& g, const std::vector<double>& centers, float x0, float width, float y)
{
    if (centers.empty())
        return;

    const auto barWidth = width / (float) centers.size();
    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (const auto target : { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
    {
        int best = -1;
        double bestDistance = 1.0e9;
        for (size_t i = 0; i < centers.size(); ++i)
            if (centers[i] > 0.0)
                if (const auto d = std::abs (std::log2 (centers[i] / target)); d < bestDistance)
                {
                    bestDistance = d;
                    best = (int) i;
                }

        if (best < 0 || bestDistance > 0.2)
            continue;

        const auto text = target >= 1000.0 ? juce::String ((int) (target / 1000.0)) + "k"
                                           : juce::String (target, target < 50.0 ? 1 : 0);
        const auto x = x0 + ((float) best + 0.5f) * barWidth;
        g.drawText (text, (int) x - 22, (int) y, 44, 14, juce::Justification::centred);
    }
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
    latestSpl = spl;

    splLabel.setText (spl.hasSignal ? juce::String (spl.currentDb, 1) : "--",
                      juce::dontSendNotification);
    splLabel.setColour (juce::Label::textColourId,
                        splColour (spl.currentDb, greenThresholdDb, yellowThresholdDb));

    leqLabel.setText (juce::String (u8"Leq   ") + strings::splLeq1min + ": "
                          + juce::String (spl.leq1minDb, 1) + "   " + strings::splLeq15min + ": "
                          + juce::String (spl.leq15minDb, 1) + "   " + strings::splLeqSession + ": "
                          + juce::String (spl.leqSessionDb, 1),
                      juce::dontSendNotification);

    // 重み付けと時定数を必ず併記する（issue #139: 何を測っている数字か分からなかった）
    {
        const auto& analyzer = engine.getSplAnalyzer();
        const auto weighting = analyzer.getWeighting();
        const auto unit = juce::String (weighting == eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted ? "dB(A)"
                                        : weighting == eeq::dsp::RtaSplAnalyzer::Weighting::cWeighted ? "dB(C)"
                                                                                                      : "dB(Z)")
                        + (analyzer.getTimeConstant() == eeq::dsp::RtaSplAnalyzer::TimeConstant::slow ? " Slow" : " Fast");

        unitText = unit;
        peakLabel.setText (unit + "    " + strings::splPeakLabel + ": " + juce::String (spl.peakDb, 1) + " dB",
                           juce::dontSendNotification);
    }

    sessionLabel.setText (juce::String ((int) (spl.sessionSeconds / 60.0)) + " min",
                          juce::dontSendNotification);

    // 校正済みなら「未校正」注記を出さない（issue #139）
    {
        calibrated = std::abs (engine.getSplAnalyzer().getCalibrationOffsetDb()) > 1.0e-9;
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
    // 見出し（上 26px）と周波数の目盛り（下 18px）を除いた中
    return historyArea.toFloat().reduced (12.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
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
    // 暗い会場で目に刺さらない、ほぼ黒の面。数字だけが状態の色で光る（2026-11 リデザイン）
    g.fillAll (theme::mode() == theme::Mode::dark ? juce::Colour (0xff07090b)
                                                  : theme::get (theme::ColourId::windowBackground));

    const auto& spl = latestSpl;
    const auto stateColour = splColour (spl.currentDb, greenThresholdDb, yellowThresholdDb);
    const auto dim = theme::get (theme::ColourId::textDim);
    const auto text = theme::get (theme::ColourId::text);
    const auto line = theme::get (theme::ColourId::controlOutline);

    // しきい値超過は画面全体の枠でも知らせる（仕様 5.4）
    if (spl.hasSignal && spl.currentDb >= greenThresholdDb)
    {
        g.setColour (stateColour.withAlpha (0.85f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 12.0f, 4.0f);
    }

    // ---- ヘッダー: 本番 / 時刻 / 記録 ----
    {
        auto r = headerArea;
        g.setColour (theme::get (theme::ColourId::accent));
        g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawText ("LIVE", r.removeFromLeft (64), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (juce::Font (juce::FontOptions (22.0f)));
        g.drawText (juce::Time::getCurrentTime().formatted ("%H:%M"), r.removeFromLeft (90), juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.setColour (loggingActive ? theme::get (theme::ColourId::danger) : dim);
        g.drawText (loggingActive ? juce::String (u8"● ") + strings::liveRecording : juce::String (strings::liveLogInactive),
                    r.removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (dim);
        g.drawText (juce::String ((int) (spl.sessionSeconds / 60.0)) + strings::liveMinutesSuffix, r.removeFromLeft (110),
                    juce::Justification::centredLeft);
    }

    // ---- 大きな数字 ----
    {
        auto r = numberArea;
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawText (strings::liveCurrentLevel + "  " + unitText, r.removeFromTop (24), juce::Justification::bottomLeft);

        const auto value = spl.hasSignal ? juce::String (spl.currentDb, 1) : juce::String ("--");
        auto size = juce::jlimit (60.0f, 260.0f, (float) r.getHeight() * 0.9f);
        juce::Font big { juce::FontOptions (size) };
        // 幅に収まるまで縮める（「-100.0」のような桁でも単位まで入るように）
        while (size > 60.0f && juce::GlyphArrangement::getStringWidth (big, value) + size * 0.5f > (float) r.getWidth())
            big = juce::Font (juce::FontOptions (size -= 8.0f));

        const auto valueWidth = juce::GlyphArrangement::getStringWidth (big, value);
        g.setFont (big);
        g.setColour (spl.hasSignal ? stateColour : dim);
        g.drawText (value, r, juce::Justification::centredLeft);

        // 単位は数字の右下（ベースライン付近）に添える
        const auto baseline = (float) r.getCentreY() + big.getAscent() * 0.5f - big.getDescent() * 0.5f;
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (18.0f, size * 0.2f))));
        g.drawText ("dB", juce::Rectangle<float> ((float) r.getX() + valueWidth + size * 0.08f, baseline - size * 0.24f,
                                                  size * 0.5f, size * 0.24f).toNearestInt(),
                    juce::Justification::bottomLeft);
    }

    // ---- 横のレベルバー（60〜120 dB、しきい値の目盛り付き）----
    {
        const auto bar = meterArea.toFloat().withHeight (10.0f).withY ((float) meterArea.getY() + 4.0f);
        const auto toX = [&bar] (double db) { return bar.getX() + (float) juce::jlimit (0.0, 1.0, (db - 60.0) / 60.0) * bar.getWidth(); };

        g.setColour (theme::get (theme::ColourId::raised));
        g.fillRoundedRectangle (bar, 5.0f);

        if (spl.hasSignal)
        {
            g.setColour (stateColour);
            g.fillRoundedRectangle (bar.withRight (toX (spl.currentDb)), 5.0f);
            g.setColour (text.withAlpha (0.8f));
            g.fillRect (toX (spl.peakDb) - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
        }

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        for (const auto& [db, colour] : { std::pair<int, juce::Colour> { greenThresholdDb, theme::get (theme::ColourId::warn) },
                                          std::pair<int, juce::Colour> { yellowThresholdDb, theme::get (theme::ColourId::danger) } })
        {
            const auto x = toX (db);
            g.setColour (colour);
            g.fillRect (x - 1.0f, bar.getY() - 5.0f, 2.0f, bar.getHeight() + 10.0f);
            g.drawText (juce::String (db), (int) x - 20, (int) bar.getBottom() + 6, 40, 16, juce::Justification::centred);
        }

        g.setColour (dim);
        for (int db = 60; db <= 120; db += 10)
            if (db != greenThresholdDb && db != yellowThresholdDb)
                g.drawText (juce::String (db), (int) toX (db) - 20, (int) bar.getBottom() + 6, 40, 16, juce::Justification::centred);
    }

    // ---- 統計のタイル ----
    {
        auto r = tilesArea;
        const auto w = (r.getWidth() - 3 * 10) / 4;
        auto tile = [&] (const juce::String& title, double value)
        {
            const auto valueText = value > -200.0 && spl.hasSignal ? juce::String (value, 1) : juce::String ("--");
            paintTile (g, r.removeFromLeft (w), title, valueText,
                       value > -200.0 && spl.hasSignal ? splColour (value, greenThresholdDb, yellowThresholdDb) : dim);
            r.removeFromLeft (10);
        };
        tile ("Leq " + strings::splLeq1min, spl.leq1minDb);
        tile ("Leq " + strings::splLeq15min, spl.leq15minDb);
        tile ("Leq " + strings::splLeqSession, spl.leqSessionDb);
        tile (strings::splPeakLabel, spl.peakDb);
    }

    // ---- 右: 推移（またはウォーターフォール）と RTA ----
    for (const auto& card : { historyArea, rtaArea })
    {
        g.setColour (theme::get (theme::ColourId::graphBackground).withAlpha (0.7f));
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (line.withAlpha (0.6f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    const auto graph = graphArea();
    if (waterfallVisible)
        paintWaterfall (g, graph);
    else
        paintHistory (g, graph, stateColour);

    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (waterfallVisible ? strings::liveWaterfallToggle + juce::String (u8"（下が現在）") : juce::String (strings::liveSplHistory),
                historyArea.reduced (12, 6).removeFromTop (16), juce::Justification::centredLeft);
    g.drawText ("RTA", rtaArea.reduced (12, 6).removeFromTop (16), juce::Justification::centredLeft);
    paintRta (g, rtaArea.toFloat().reduced (12.0f, 6.0f).withTrimmedTop (22.0f));

    // ---- フッター ----
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (strings::liveLongPressHint + (calibrated ? juce::String() : juce::String (u8"   ·   ") + strings::splUncalibrated),
                footerArea, juce::Justification::centredLeft, true);
}

void LiveModeOverlay::paintTile (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                 const juce::String& value, juce::Colour valueColour) const
{
    g.setColour (theme::get (theme::ColourId::raised).withAlpha (0.8f));
    g.fillRoundedRectangle (area.toFloat(), 10.0f);
    auto r = area.reduced (14, 10);
    g.setColour (theme::get (theme::ColourId::textDim));
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (title, r.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (valueColour);
    g.setFont (juce::Font (juce::FontOptions (juce::jlimit (20.0f, 40.0f, (float) r.getHeight() * 0.8f))));
    g.drawText (value, r, juce::Justification::centredLeft);
}

void LiveModeOverlay::paintHistory (juce::Graphics& g, const juce::Rectangle<float>& area,
                                    juce::Colour frameColour)
{
    // しきい値の線（注意 / 上限）
    const auto yOf = [&area] (float db) { return area.getBottom() - (juce::jlimit (40.0f, 130.0f, db) - 40.0f) / 90.0f * area.getHeight(); };
    g.setColour (theme::get (theme::ColourId::warn).withAlpha (0.6f));
    g.drawHorizontalLine ((int) yOf ((float) greenThresholdDb), area.getX(), area.getRight());
    g.setColour (theme::get (theme::ColourId::danger).withAlpha (0.7f));
    g.drawHorizontalLine ((int) yOf ((float) yellowThresholdDb), area.getX(), area.getRight());

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

}

void LiveModeOverlay::paintWaterfall (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // 垂直ウォーターフォール: 時間は縦軸（下が現在）、周波数は横軸（RTAと合わせる）
    if (waterfallImage.isValid())
        g.drawImageAt (waterfallImage, (int) area.getX(), (int) area.getY());

    // 周波数ラベル（RTAと合わせる）
    drawOctaveLabels (g, rtaCenters, area.getX(), area.getWidth(), area.getBottom() + 3.0f);

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

    drawOctaveLabels (g, rtaCenters, plot.getX(), plot.getWidth(), plot.getBottom() + 1.0f);

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
        g.drawText (juce::String (juce::roundToInt (db)), (int) plot.getX() + 2, (int) y - 6, 40, 12,
                    juce::Justification::centredLeft);
    }
}

void LiveModeOverlay::resized()
{
    // 2026-11 リデザイン: 左に大きな数字と統計、右に推移と RTA。操作はヘッダー右にまとめる
    for (auto* label : { &splLabel, &leqLabel, &peakLabel, &sessionLabel, &hintLabel })
        label->setVisible (false);

    auto bounds = getLocalBounds().reduced (24, 16);

    headerArea = bounds.removeFromTop (44);
    {
        auto controls = headerArea;
        exitButton.setBounds (controls.removeFromRight (150).withSizeKeepingCentre (150, 36));
        controls.removeFromRight (10);
        waterfallButton.setBounds (controls.removeFromRight (150).withSizeKeepingCentre (150, 36));
        controls.removeFromRight (18);
        limitThresholdCombo.setBounds (controls.removeFromRight (92).withSizeKeepingCentre (92, 32));
        limitThresholdTitle.setBounds (controls.removeFromRight (40));
        controls.removeFromRight (10);
        warnThresholdCombo.setBounds (controls.removeFromRight (92).withSizeKeepingCentre (92, 32));
        warnThresholdTitle.setBounds (controls.removeFromRight (40));
        controls.removeFromRight (18);
        outputBadge.setBounds (controls.removeFromRight (juce::jmin (300, juce::jmax (0, controls.getWidth() - 430)))
                                   .withSizeKeepingCentre (juce::jmin (300, juce::jmax (0, controls.getWidth() - 430)), 32));
    }

    footerArea = bounds.removeFromBottom (24);
    bounds.removeFromTop (18);
    bounds.removeFromBottom (10);

    auto left = bounds.removeFromLeft ((int) (bounds.getWidth() * 0.54f));
    bounds.removeFromLeft (24);
    auto right = bounds;

    tilesArea = left.removeFromBottom (juce::jlimit (80, 110, left.getHeight() / 5));
    left.removeFromBottom (24);
    meterArea = left.removeFromBottom (40);
    left.removeFromBottom (12);
    numberArea = left;

    historyArea = right.removeFromTop ((int) (right.getHeight() * 0.56f));
    right.removeFromTop (14);
    rtaArea = right;

    waterfallImage = juce::Image(); // 大きさが変わったら作り直す
}

} // namespace eeq::ui
