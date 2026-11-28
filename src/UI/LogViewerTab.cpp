#include "LogViewerTab.h"

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

namespace
{
    // 帯域バランスの 3 区分
    constexpr double lowLo = 30.0, lowHi = 160.0, midLo = 200.0, midHi = 2500.0, highLo = 3150.0, highHi = 20000.0;

    juce::Colour heat (float t)
    {
        // 暗い紺 → シアン → 黄 → 白（暗い背景で、強いところだけがはっきり目立つ）
        t = juce::jlimit (0.0f, 1.0f, t);
        const juce::Colour stops[] = { juce::Colour (0xff0d1420), juce::Colour (0xff15405a), juce::Colour (0xff1f9fb0),
                                       juce::Colour (0xffe8c547), juce::Colour (0xfffff4d6) };
        const auto pos = t * 4.0f;
        const auto i = juce::jmin (3, (int) pos);
        return stops[i].interpolatedWith (stops[i + 1], pos - (float) i);
    }

    juce::String formatSeconds (double s)
    {
        const auto total = juce::jmax (0, (int) std::lround (s));
        const auto h = total / 3600, m = (total / 60) % 60, sec = total % 60;
        return h > 0 ? juce::String::formatted ("%d:%02d:%02d", h, m, sec) : juce::String::formatted ("%d:%02d", m, sec);
    }

    // 1/3 oct の公称周波数（ログには正確な中心周波数が入っているので、表示だけ丸める）
    double nominal (double hz)
    {
        static const double series[] = { 10, 12.5, 16, 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
                                         630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000,
                                         12500, 16000, 20000 };
        double best = hz;
        for (auto n : series)
            if (std::abs (std::log (hz / n)) < std::abs (std::log (hz / best)) || best == hz)
                best = n;
        return std::abs (std::log (hz / best)) < 0.06 ? best : hz;
    }

    juce::String bandLabel (double rawHz)
    {
        const auto hz = nominal (rawHz);
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k"
                            : juce::String (juce::roundToInt (hz));
    }
}

namespace
{
    /** 目盛りを付ける帯域（各公称値に一番近い帯域を 1 つだけ。1/24 oct でも重ならない） */
    std::vector<int> labelledBands (const std::vector<double>& bandHz, std::initializer_list<double> values)
    {
        std::vector<int> out;
        for (const auto v : values)
        {
            int best = -1;
            double bestDistance = 0.06;
            for (int b = 0; b < (int) bandHz.size(); ++b)
                if (const auto d = std::abs (std::log (bandHz[(size_t) b] / v)); d < bestDistance)
                {
                    bestDistance = d;
                    best = b;
                }
            if (best >= 0)
                out.push_back (best);
        }
        return out;
    }
}

juce::File LogViewerTab::logDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("EasyEQAssistant").getChildFile ("spl");
}

LogViewerTab::LogViewerTab (UiContext& context) : ctx (context)
{
    titleLabel.setText (strings::logTitle, juce::dontSendNotification);
    styleLabel (titleLabel, 16.0f, false);
    addAndMakeVisible (titleLabel);

    styleLabel (infoLabel, 13.0f, true);
    addAndMakeVisible (infoLabel);

    styleCombo (fileCombo);
    fileCombo.setTextWhenNothingSelected (strings::logNoFiles);
    fileCombo.onChange = [this] { loadSelected (false); };
    addAndMakeVisible (fileCombo);

    styleButton (reloadButton);
    reloadButton.setButtonText (strings::logReload);
    reloadButton.onClick = [this] { refreshFileList(); };
    addAndMakeVisible (reloadButton);

    styleButton (folderButton);
    folderButton.setButtonText (strings::logOpenFolder);
    folderButton.onClick = [] { logDirectory().startAsProcess(); };
    addAndMakeVisible (folderButton);

    refreshFileList();
}

void LogViewerTab::refreshFileList()
{
    const auto previous = loadedFile;
    files = logDirectory().findChildFiles (juce::File::findFiles, false, "*_spectrum*.csv");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });

    fileCombo.clear (juce::dontSendNotification);
    int selectId = 0;

    for (int i = 0; i < files.size(); ++i)
    {
        // 20261128_173247_spectrum.csv → 2026-11-28 17:32:47
        const auto name = files[i].getFileNameWithoutExtension();
        auto label = name;
        if (name.length() >= 15 && name[8] == '_')
            label = name.substring (0, 4) + "-" + name.substring (4, 6) + "-" + name.substring (6, 8) + "  "
                  + name.substring (9, 11) + ":" + name.substring (11, 13) + ":" + name.substring (13, 15)
                  + (name.contains ("_spectrum_") ? juce::String (u8"（") + name.fromLastOccurrenceOf ("_", false, false) + juce::String (u8"）") : juce::String());
        fileCombo.addItem (label, i + 1);
        if (files[i] == previous)
            selectId = i + 1;
    }

    if (files.isEmpty())
    {
        log = eeq::core::SpectrumLog();
        loadedFile = juce::File();
        rebuildDerived();
        repaint();
        return;
    }

    fileCombo.setSelectedId (selectId > 0 ? selectId : 1, juce::dontSendNotification);
    loadSelected (selectId > 0);
}

void LogViewerTab::loadSelected (bool keepView)
{
    const auto index = fileCombo.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (index, files.size()))
        return;

    const auto file = files[index];
    juce::String error;
    eeq::core::SpectrumLog parsed;

    if (! eeq::core::SpectrumLog::parse (file.loadFileAsString(), parsed, error))
    {
        log = eeq::core::SpectrumLog();
        infoLabel.setText (error, juce::dontSendNotification);
    }
    else
    {
        const auto wasAtEnd = viewEnd >= (log.rows() > 0 ? log.seconds.back() - 1.0 : 0.0);
        log = std::move (parsed);

        if (! keepView || loadedFile != file)
        {
            viewStart = 0.0;
            viewEnd = juce::jmax (10.0, log.seconds.back());
            pinnedRow = -1;
        }
        else if (wasAtEnd)
        {
            // 記録中のファイルを読み直したとき、最後まで表示していたら新しい分まで広げる
            const auto width = viewEnd - viewStart;
            viewEnd = juce::jmax (10.0, log.seconds.back());
            viewStart = juce::jmax (0.0, viewEnd - width);
        }
    }

    loadedFile = file;
    loadedModified = file.getLastModificationTime();
    rebuildDerived();
    repaint();
}

void LogViewerTab::rebuildDerived()
{
    averageSpectrum.clear();
    lowDelta.clear();
    midDelta.clear();
    highDelta.clear();
    spectrogram = {};

    const auto rows = log.rows(), bands = log.bands();
    if (rows == 0 || bands == 0)
        return;

    // 平均スペクトルと色の範囲（上位の強いところが黄〜白、下 60 dB を色にする）
    std::vector<double> energy ((size_t) bands, 0.0);
    float maxDb = -200.0f;
    for (const auto& row : log.levels)
        for (int b = 0; b < bands; ++b)
        {
            energy[(size_t) b] += std::pow (10.0, row[(size_t) b] / 10.0);
            maxDb = std::max (maxDb, row[(size_t) b]);
        }
    for (int b = 0; b < bands; ++b)
        averageSpectrum.push_back ((float) (10.0 * std::log10 (std::max (energy[(size_t) b] / rows, 1.0e-30))));
    specMaxDb = std::ceil (maxDb / 5.0f) * 5.0f;
    specMinDb = specMaxDb - 60.0f;

    // 帯域バランス: 低域・高域を中域との差（dB）で表し、本番平均との差にする（10 秒平滑）。
    // 全体に対する割合にすると、低音が増えたときに中高域が「減った」ように見えてしまうので中域を基準にする。
    // 中域の線は SPL（全体の音量に対する中域の割合）の変化を示す
    auto series = [&] (double lo, double hi, bool againstTotal)
    {
        std::vector<double> raw ((size_t) rows);
        for (int r = 0; r < rows; ++r)
            raw[(size_t) r] = log.bandSumDb (r, lo, hi) - (againstTotal ? log.bandSumDb (r, 0.0, 1.0e9) : log.bandSumDb (r, midLo, midHi));
        double mean = 0.0;
        for (auto v : raw) mean += v;
        mean /= rows;
        std::vector<float> out ((size_t) rows);
        double acc = 0.0;
        for (int r = 0; r < rows; ++r)
        {
            acc += raw[(size_t) r];
            if (r >= 10) acc -= raw[(size_t) r - 10];
            out[(size_t) r] = (float) (acc / std::min (r + 1, 10) - mean);
        }
        return out;
    };
    lowDelta = series (lowLo, lowHi, false);
    midDelta = series (midLo, midHi, true);
    highDelta = series (highLo, highHi, false);

    // スペクトログラム画像（横 = 行、縦 = 帯域。上が高域）
    spectrogram = juce::Image (juce::Image::RGB, rows, bands, false);
    for (int r = 0; r < rows; ++r)
        for (int b = 0; b < bands; ++b)
            spectrogram.setPixelAt (r, bands - 1 - b, heat ((log.levels[(size_t) r][(size_t) b] - specMinDb) / (specMaxDb - specMinDb)));

    // 情報行
    double leqEnergy = 0.0;
    for (auto v : log.spl) leqEnergy += std::pow (10.0, v / 10.0);
    infoLabel.setText (log.clock.front() + juce::String (u8" 〜 ") + log.clock.back() + juce::String (u8"（") + formatSeconds (log.seconds.back()) + juce::String (u8"）   ")
                           + strings::logOverallLeq + juce::String (10.0 * std::log10 (leqEnergy / rows), 1) + " dB",
                       juce::dontSendNotification);
}

void LogViewerTab::timerTick()
{
    // 記録中（本番モード中）のファイルは 5 秒ごとに読み直す
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPollMs < 5000)
        return;
    lastPollMs = now;

    if (loadedFile.existsAsFile() && loadedFile.getLastModificationTime() != loadedModified)
        loadSelected (true);
    else if (files.isEmpty() || (files.size() > 0 && logDirectory().findChildFiles (juce::File::findFiles, false, "*_spectrum*.csv").size() != files.size()))
        refreshFileList();
}

int LogViewerTab::rowAtX (float x) const
{
    if (log.rows() == 0 || specArea.getWidth() <= 0.0f)
        return -1;
    const auto s = viewStart + (double) (x - specArea.getX()) / specArea.getWidth() * (viewEnd - viewStart);
    const auto it = std::lower_bound (log.seconds.begin(), log.seconds.end(), s);
    auto index = (int) std::distance (log.seconds.begin(), it);
    if (index > 0 && (index == log.rows() || s - log.seconds[(size_t) index - 1] < log.seconds[(size_t) index] - s))
        --index;
    return juce::jlimit (0, log.rows() - 1, index);
}

float LogViewerTab::xForSeconds (double s) const
{
    return specArea.getX() + (float) ((s - viewStart) / (viewEnd - viewStart)) * specArea.getWidth();
}

void LogViewerTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    for (const auto& card : cardRects)
    {
        g.setColour (theme::get (theme::ColourId::raised));
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (theme::get (theme::ColourId::controlOutline));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    if (log.rows() == 0)
    {
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawFittedText (strings::logEmptyHint, plotCard.reduced (40), juce::Justification::centred, 4);
        return;
    }

    drawSpl (g, splArea);
    drawSpectrogram (g, specArea);
    drawBalance (g, balanceArea);
    drawTimeAxis (g, balanceArea, balanceArea.getBottom() + 4.0f);

    // カーソル（固定があればそちら）
    const auto row = pinnedRow >= 0 ? pinnedRow : cursorRow;
    if (row >= 0)
    {
        const auto x = xForSeconds (log.seconds[(size_t) row]);
        if (x >= specArea.getX() && x <= specArea.getRight())
        {
            g.setColour (theme::get (theme::ColourId::text).withAlpha (pinnedRow >= 0 ? 0.9f : 0.5f));
            g.drawVerticalLine ((int) x, splArea.getY(), balanceArea.getBottom());
        }
    }

    drawSidePanel (g, sideCard.reduced (16, 12));
}

void LogViewerTab::drawSpl (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto dim = theme::get (theme::ColourId::textDim);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (strings::logSplCaption, area.withHeight (16).translated (0, -18), juce::Justification::centredLeft);

    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (area, 4.0f);

    float lo = 1000.0f, hi = -1000.0f;
    for (int r = 0; r < log.rows(); ++r)
        if (log.seconds[(size_t) r] >= viewStart && log.seconds[(size_t) r] <= viewEnd)
        {
            lo = std::min (lo, log.spl[(size_t) r]);
            hi = std::max (hi, log.spl[(size_t) r]);
        }
    if (lo > hi) { lo = 60.0f; hi = 100.0f; }
    lo = std::floor (lo / 5.0f) * 5.0f - 5.0f;
    hi = std::ceil (hi / 5.0f) * 5.0f + 5.0f;
    auto toY = [&] (float db) { return area.getBottom() - (db - lo) / (hi - lo) * area.getHeight(); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db = lo; db <= hi; db += (hi - lo) > 30.0f ? 10.0f : 5.0f)
    {
        g.setColour (theme::get (theme::ColourId::grid));
        g.drawHorizontalLine ((int) toY (db), area.getX(), area.getRight());
        g.setColour (dim);
        g.drawText (juce::String (juce::roundToInt (db)), (int) area.getX() - 40, (int) toY (db) - 7, 34, 14, juce::Justification::centredRight);
    }

    auto drawSeries = [&] (const std::vector<float>& values, juce::Colour colour, float thickness)
    {
        juce::Path path;
        bool started = false;
        for (int r = 0; r < log.rows(); ++r)
        {
            const auto s = log.seconds[(size_t) r];
            if (s < viewStart - 2.0 || s > viewEnd + 2.0)
                continue;
            const auto p = juce::Point<float> (xForSeconds (s), toY (values[(size_t) r]));
            if (! started) { path.startNewSubPath (p); started = true; } else path.lineTo (p);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness));
    };

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    drawSeries (log.spl, theme::get (theme::ColourId::accent).withAlpha (0.45f), 1.0f);
    drawSeries (log.leq1min, theme::get (theme::ColourId::accent), 2.0f);
    g.restoreState();
}

void LogViewerTab::drawSpectrogram (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto dim = theme::get (theme::ColourId::textDim);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (strings::logSpectrogramCaption + juce::String (juce::roundToInt (specMinDb)) + juce::String (u8"〜") + juce::String (juce::roundToInt (specMaxDb)) + " dB",
                area.withHeight (16).translated (0, -18), juce::Justification::centredLeft);

    if (! spectrogram.isValid())
        return;

    // 表示範囲の行だけを切り出して引き伸ばす（ぼかさない）
    const auto first = juce::jlimit (0, log.rows() - 1, rowAtX (area.getX()));
    const auto last = juce::jlimit (first, log.rows() - 1, rowAtX (area.getRight()));
    const auto x0 = xForSeconds (log.seconds[(size_t) first]);
    const auto x1 = xForSeconds (log.seconds[(size_t) last] + 1.0);

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (spectrogram, (int) x0, (int) area.getY(), juce::jmax (1, (int) (x1 - x0)), (int) area.getHeight(),
                 first, 0, last - first + 1, log.bands());
    g.restoreState();

    // 周波数の目盛り
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    const auto bands = log.bands();
    for (const auto b : labelledBands (log.bandHz, { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 }))
    {
        const auto y = area.getY() + (float) (bands - 1 - b + 0.5f) / (float) bands * area.getHeight();
        g.setColour (dim);
        g.drawText (bandLabel (log.bandHz[(size_t) b]), (int) area.getX() - 40, (int) y - 7, 34, 14, juce::Justification::centredRight);
    }
}

void LogViewerTab::drawBalance (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto dim = theme::get (theme::ColourId::textDim);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (strings::logBalanceCaption, area.withHeight (16).translated (0, -18), juce::Justification::centredLeft);

    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (area, 4.0f);

    const float range = 9.0f;
    auto toY = [&] (float db) { return area.getCentreY() - juce::jlimit (-range, range, db) / range * area.getHeight() * 0.5f; };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { -6.0f, -3.0f, 0.0f, 3.0f, 6.0f })
    {
        g.setColour (theme::get (db == 0.0f ? theme::ColourId::gridMajor : theme::ColourId::grid));
        g.drawHorizontalLine ((int) toY (db), area.getX(), area.getRight());
        g.setColour (dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (juce::roundToInt (db)), (int) area.getX() - 40, (int) toY (db) - 7, 34, 14,
                    juce::Justification::centredRight);
    }

    const juce::Colour colours[] = { theme::get (theme::ColourId::danger).brighter (0.2f), theme::get (theme::ColourId::ok),
                                     theme::get (theme::ColourId::accent2) };
    const std::vector<float>* series[] = { &lowDelta, &midDelta, &highDelta };

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    for (int i = 0; i < 3; i += 2) // 低域と高域（どちらも中域との比）
    {
        juce::Path path;
        bool started = false;
        for (int r = 0; r < log.rows(); ++r)
        {
            const auto s = log.seconds[(size_t) r];
            if (s < viewStart - 10.0 || s > viewEnd + 10.0)
                continue;
            const auto p = juce::Point<float> (xForSeconds (s), toY ((*series[i])[(size_t) r]));
            if (! started) { path.startNewSubPath (p); started = true; } else path.lineTo (p);
        }
        g.setColour (colours[i]);
        g.strokePath (path, juce::PathStrokeType (1.6f));
    }
    g.restoreState();

    // 凡例
    auto legend = area.reduced (8, 4).removeFromTop (16);
    const juce::String names[] = { strings::logBandLow, strings::logBandMid, strings::logBandHigh };
    for (int i = 2; i >= 0; i -= 2)
    {
        auto item = legend.removeFromRight (136);
        g.setColour (colours[i]);
        g.fillRect (item.removeFromLeft (14).withSizeKeepingCentre (14, 3));
        g.setColour (dim);
        g.drawText (names[i], item.withTrimmedLeft (4), juce::Justification::centredLeft);
    }
}

void LogViewerTab::drawTimeAxis (juce::Graphics& g, juce::Rectangle<float> plot, float y)
{
    const auto span = viewEnd - viewStart;
    double step = 1.0;
    for (const auto candidate : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0 })
        if (span / candidate <= 10.0) { step = candidate; break; }

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (double s = std::ceil (viewStart / step) * step; s <= viewEnd; s += step)
    {
        const auto x = xForSeconds (s);
        g.setColour (theme::get (theme::ColourId::grid).withAlpha (0.3f));
        g.drawVerticalLine ((int) x, splArea.getY(), plot.getBottom());
        g.setColour (theme::get (theme::ColourId::textDim));
        g.drawText (formatSeconds (s), (int) x - 40, (int) y, 80, 14, juce::Justification::centred);
    }
}

void LogViewerTab::drawSidePanel (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto dim = theme::get (theme::ColourId::textDim);
    const auto text = theme::get (theme::ColourId::text);
    const auto row = pinnedRow >= 0 ? pinnedRow : (cursorRow >= 0 ? cursorRow : log.rows() - 1);

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (strings::logCursorTitle, area.removeFromTop (24), juce::Justification::centredLeft);

    g.setFont (juce::Font (juce::FontOptions (28.0f)));
    g.drawText (log.clock[(size_t) row], area.removeFromTop (38), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (juce::String (u8"開始から ") + formatSeconds (log.seconds[(size_t) row]) + juce::String (u8"   SPL ") + juce::String (log.spl[(size_t) row], 1)
                    + juce::String (u8" / Leq1分 ") + juce::String (log.leq1min[(size_t) row], 1),
                area.removeFromTop (20), juce::Justification::centredLeft);
    area.removeFromTop (10);

    // 帯域バランス（数値）
    const juce::String names[] = { strings::logBandLow, strings::logBandMid, strings::logBandHigh };
    const std::vector<float>* series[] = { &lowDelta, &midDelta, &highDelta };
    for (int i = 0; i < 3; i += 2)
    {
        auto line = area.removeFromTop (22);
        const auto v = (*series[i])[(size_t) row];
        g.setColour (dim);
        g.drawText (names[i], line.removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (std::abs (v) >= 3.0f ? theme::get (theme::ColourId::warn) : text);
        g.drawText ((v >= 0 ? "+" : "") + juce::String (v, 1) + juce::String (u8" dB"), line, juce::Justification::centredLeft);
    }
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawFittedText (strings::logBalanceNote, area.removeFromTop (34), juce::Justification::topLeft, 2);
    area.removeFromTop (8);

    // スペクトル（棒 = この時点の 5 秒平均、線 = 本番全体の平均）
    auto plot = area.withTrimmedBottom (18).toFloat();
    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (plot, 4.0f);
    const auto bands = log.bands();
    if (bands == 0 || plot.getHeight() < 40.0f)
        return;

    std::vector<float> now ((size_t) bands);
    {
        const auto r0 = juce::jmax (0, row - 2), r1 = juce::jmin (log.rows() - 1, row + 2);
        for (int b = 0; b < bands; ++b)
        {
            double e = 0.0;
            for (int r = r0; r <= r1; ++r)
                e += std::pow (10.0, log.levels[(size_t) r][(size_t) b] / 10.0);
            now[(size_t) b] = (float) (10.0 * std::log10 (std::max (e / (r1 - r0 + 1), 1.0e-30)));
        }
    }

    auto toY = [&] (float db) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db - specMinDb) / (specMaxDb - specMinDb)) * plot.getHeight(); };
    const auto w = plot.getWidth() / (float) bands;
    for (int b = 0; b < bands; ++b)
    {
        const auto x = plot.getX() + w * (float) b;
        const auto top = toY (now[(size_t) b]);
        g.setColour (theme::get (theme::ColourId::accent).withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (x + w * 0.12f, top, w * 0.76f, plot.getBottom() - top));
    }

    juce::Path avg;
    for (int b = 0; b < bands; ++b)
    {
        const juce::Point<float> p (plot.getX() + w * ((float) b + 0.5f), toY (averageSpectrum[(size_t) b]));
        if (b == 0) avg.startNewSubPath (p); else avg.lineTo (p);
    }
    g.setColour (text.withAlpha (0.85f));
    g.strokePath (avg, juce::PathStrokeType (1.5f));

    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    for (const auto b : labelledBands (log.bandHz, { 63.0, 250.0, 1000.0, 4000.0, 16000.0 }))
        g.drawText (bandLabel (log.bandHz[(size_t) b]), (int) (plot.getX() + w * ((float) b + 0.5f)) - 20, (int) plot.getBottom() + 2, 40, 14,
                    juce::Justification::centred);
    g.drawText (strings::logSpectrumLegend, plot.reduced (6, 4), juce::Justification::topRight);
}

void LogViewerTab::mouseMove (const juce::MouseEvent& e)
{
    const auto inside = e.position.x >= specArea.getX() && e.position.x <= specArea.getRight()
                     && e.position.y >= splArea.getY() && e.position.y <= balanceArea.getBottom();
    const auto row = inside ? rowAtX (e.position.x) : -1;
    if (row != cursorRow)
    {
        cursorRow = row;
        repaint();
    }
}

void LogViewerTab::mouseExit (const juce::MouseEvent&)
{
    if (cursorRow >= 0)
    {
        cursorRow = -1;
        repaint();
    }
}

void LogViewerTab::mouseDown (const juce::MouseEvent& e)
{
    dragStartX = e.position.x;
    dragStartView = viewStart;
    const auto inside = e.position.x >= specArea.getX() && e.position.x <= specArea.getRight()
                     && e.position.y >= splArea.getY() && e.position.y <= balanceArea.getBottom();
    if (inside)
    {
        const auto row = rowAtX (e.position.x);
        pinnedRow = row == pinnedRow ? -1 : row; // 同じ所をもう一度クリックで固定を外す
        repaint();
    }
}

void LogViewerTab::mouseDrag (const juce::MouseEvent& e)
{
    // 横ドラッグで時間をずらす（拡大しているとき）
    if (log.rows() == 0 || std::abs (e.position.x - dragStartX) < 4.0f)
        return;
    const auto width = viewEnd - viewStart;
    const auto total = juce::jmax (10.0, log.seconds.back());
    auto start = dragStartView - (double) (e.position.x - dragStartX) / specArea.getWidth() * width;
    start = juce::jlimit (0.0, juce::jmax (0.0, total - width), start);
    viewStart = start;
    viewEnd = start + width;
    pinnedRow = -1;
    repaint();
}

void LogViewerTab::mouseDoubleClick (const juce::MouseEvent&)
{
    if (log.rows() == 0)
        return;
    viewStart = 0.0;
    viewEnd = juce::jmax (10.0, log.seconds.back());
    repaint();
}

void LogViewerTab::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (log.rows() == 0 || specArea.getWidth() <= 0.0f)
        return;
    const auto total = juce::jmax (10.0, log.seconds.back());
    const auto anchor = viewStart + (double) (e.position.x - specArea.getX()) / specArea.getWidth() * (viewEnd - viewStart);
    const auto factor = std::pow (0.8, (double) wheel.deltaY * 4.0);
    auto width = juce::jlimit (20.0, total, (viewEnd - viewStart) * factor);
    auto start = anchor - (anchor - viewStart) * width / (viewEnd - viewStart);
    start = juce::jlimit (0.0, juce::jmax (0.0, total - width), start);
    viewStart = start;
    viewEnd = start + width;
    repaint();
}

void LogViewerTab::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    cardRects.clear();

    auto header = bounds.removeFromTop (64);
    cardRects.push_back (header);
    bounds.removeFromTop (14);
    auto h = header.reduced (16, 10);
    folderButton.setBounds (h.removeFromRight (150).withSizeKeepingCentre (150, 32));
    h.removeFromRight (8);
    reloadButton.setBounds (h.removeFromRight (110).withSizeKeepingCentre (110, 32));
    h.removeFromRight (8);
    fileCombo.setBounds (h.removeFromRight (280).withSizeKeepingCentre (280, 32));
    h.removeFromRight (12);
    titleLabel.setBounds (h.removeFromTop (22));
    infoLabel.setBounds (h);

    sideCard = bounds.removeFromRight (juce::jmin (340, bounds.getWidth() / 3));
    bounds.removeFromRight (14);
    plotCard = bounds;
    cardRects.push_back (plotCard);
    cardRects.push_back (sideCard);

    auto plot = plotCard.reduced (16, 12).withTrimmedLeft (44).toFloat();
    plot.removeFromBottom (20); // 時間の目盛り
    const auto total = plot.getHeight();
    plot.removeFromTop (20);
    splArea = plot.removeFromTop (total * 0.22f);
    plot.removeFromTop (26);
    balanceArea = plot.removeFromBottom (total * 0.24f);
    plot.removeFromBottom (26);
    specArea = plot;
}

} // namespace eeq::ui
