#include "LimitTestTab.h"

#include "../Core/Log.h"
#include "../Core/Utilities.h"
#include "../DSP/SweepGenerator.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

namespace
{
constexpr const char* settingsCeilingKey = "limitTestCeilingDb";
constexpr const char* settingsStepKey = "limitTestStepDb";
constexpr const char* settingsDurationKey = "limitTestDurationSec";
constexpr const char* settingsAmpGainKey = "limitTestAmpGainDb";
constexpr const char* settingsSpeakerKey = "limitTestSpeakerMaxW";
constexpr const char* settingsSpeakerOhmKey = "limitTestSpeakerOhm";
} // namespace

void LimitTestTab::StepGraph::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::graphBackground));

    if (levels.size() < 2)
        return;

    const auto area = getLocalBounds().toFloat().reduced (6.0f);
    const auto levelMin = levels.front();
    const auto levelMax = levels.back();

    float splMin = 1.0e9f, splMax = -1.0e9f;

    for (const auto spl : spl)
    {
        splMin = std::min (splMin, (float) spl);
        splMax = std::max (splMax, (float) spl);
    }

    if (splMax <= splMin)
        return;

    splMin -= 3.0f;
    splMax += 3.0f;

    auto xOf = [&] (double level)
    { return area.getX() + (float) ((level - levelMin) / std::max (1.0e-9, levelMax - levelMin)) * area.getWidth(); };

    auto yOf = [&] (double spl)
    { return area.getBottom() - (float) ((spl - splMin) / (splMax - splMin)) * area.getHeight(); };

    // SPLカーブ
    juce::Path path;

    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto x = xOf (levels[i]);
        const auto y = yOf (spl[i]);

        if (path.isEmpty())
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (theme::get (theme::ColourId::accent));
    g.strokePath (path, juce::PathStrokeType (1.6f));

    // 横軸ラベル
    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (juce::String (levelMin, 0) + " dBFS", (int) area.getX(), (int) area.getBottom() + 2, 70, 12,
                juce::Justification::centredLeft);
    g.drawText (juce::String (levelMax, 0) + " dBFS", (int) area.getRight() - 70, (int) area.getBottom() + 2, 70, 12,
                juce::Justification::centredRight);
    g.drawText (strings::limitCompressionLabel + " / SPL", (int) area.getX() + 4, (int) area.getY() + 2, 200, 12,
                juce::Justification::centredLeft);
}

void LimitTestTab::ThermalGraph::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::graphBackground));

    if (samples.size() < 2)
        return;

    const auto area = getLocalBounds().toFloat().reduced (6.0f);
    const auto tMax = samples.back().first;

    float splMin = 1.0e9f, splMax = -1.0e9f;

    for (const auto& sample : samples)
    {
        splMin = std::min (splMin, (float) sample.second);
        splMax = std::max (splMax, (float) sample.second);
    }

    splMin -= 1.0f;
    splMax += 1.0f;

    juce::Path path;

    for (const auto& sample : samples)
    {
        const auto x = area.getX() + (float) (sample.first / std::max (1.0e-9, tMax)) * area.getWidth();
        const auto y = area.getBottom() - (float) ((sample.second - splMin) / (splMax - splMin)) * area.getHeight();

        if (path.isEmpty())
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (theme::get (theme::ColourId::accent2));
    g.strokePath (path, juce::PathStrokeType (1.6f));

    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (strings::limitThermalDrop + ": " + juce::String (samples.front().second - samples.back().second, 2)
                    + " dB (" + juce::String (tMax, 0) + "s)",
                (int) area.getX() + 4, (int) area.getY() + 2, 260, 12, juce::Justification::centredLeft);
}

LimitTestTab::LimitTestTab (UiContext& context)
    : ctx (context)
{
    title.setText (strings::limitSection, juce::dontSendNotification);
    styleLabel (title, 14.0f, false);
    addAndMakeVisible (title);

    safetyTitle.setText (strings::limitSafetySection, juce::dontSendNotification);
    styleLabel (safetyTitle, 12.5f, false);
    addAndMakeVisible (safetyTitle);

    warnLabel.setText (strings::limitWarnMessage, juce::dontSendNotification);
    warnLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::danger));
    warnLabel.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    addAndMakeVisible (warnLabel);

    styleToggle (acknowledgeToggle);
    acknowledgeToggle.setButtonText (strings::limitAcknowledge);
    addAndMakeVisible (acknowledgeToggle);

    auto makeField = [this] (juce::Label& label, juce::TextEditor& editor, const juce::String& title2,
                             const juce::String& initial, const char* persistKey = nullptr)
    {
        label.setText (title2, juce::dontSendNotification);
        styleLabel (label);
        addAndMakeVisible (label);

        styleTextEditor (editor);
        // 保存済みの入力値があれば復元する（無ければ初期値）
        editor.setText (persistKey != nullptr ? ctx.settings.getValue (persistKey, initial) : initial,
                        juce::dontSendNotification);
        editor.setInputRestrictions (8, "0123456789.-");
        editor.onTextChange = [this, &editor, persistKey]
        {
            if (persistKey != nullptr)
            {
                ctx.settings.setValue (persistKey, editor.getText());
                ctx.settings.saveIfNeeded();
            }

            updateSuggestion();
        };
        addAndMakeVisible (editor);
    };

    makeField (ceilingLabel, ceilingEditor, strings::limitCeilingLabel, "-6", settingsCeilingKey);
    makeField (stepLabel, stepEditor, strings::limitStepLabel, "3", settingsStepKey);
    makeField (durationLabel, durationEditor, strings::limitMaxDuration, "120", settingsDurationKey);
    makeField (ampGainLabel, ampGainEditor, strings::limitAmpGainLabel, "30", settingsAmpGainKey);
    makeField (speakerLabel, speakerEditor, strings::limitSpeakerRatingLabel, "600", settingsSpeakerKey);

    styleDangerButton (startButton);
    startButton.setButtonText (strings::limitStartButton);
    startButton.onClick = [this]
    {
        // 実行中はボタン文言が「停止」になるので、押したら実際に止める
        if (stepPhase != Phase::idle)
            stopTest (false);
        else
            startStepTest();
    };
    addAndMakeVisible (startButton);

    styleButton (thdButton);
    thdButton.setButtonText (strings::limitThdButton);
    thdButton.onClick = [this] { startThdTest(); };
    addAndMakeVisible (thdButton);

    styleButton (thermalButton);
    thermalButton.setButtonText (strings::limitThermalButton);
    thermalButton.onClick = [this]
    {
        if (thermalRunning)
            stopTest (false);
        else
            startThermalTest();
    };
    addAndMakeVisible (thermalButton);

    progressLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (progressLabel);

    thdLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (thdLabel);

    coherenceNoteLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (coherenceNoteLabel);

    suggestionLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::warn));
    suggestionLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    addAndMakeVisible (suggestionLabel);

    addAndMakeVisible (stepGraph);
    addAndMakeVisible (thermalGraph);
}

LimitTestTab::~LimitTestTab()
{
    // テスト中に閉じられたら必ず止める（安全設計: 仕様 3.7）
    if (stepPhase != Phase::idle || thdPhase != ThdPhase::idle || thermalRunning)
        ctx.engine.stopExcitation();
}

double LimitTestTab::currentSplFast()
{
    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    ctx.engine.getSplAnalyzer().copySpl (spl);
    return spl.hasSignal ? spl.currentDb : -300.0;
}

void LimitTestTab::startStepTest()
{
    if (! acknowledgeToggle.getToggleState())
    {
        progressLabel.setText (strings::limitAcknowledge + juce::String (u8" にチェックしてください"), juce::dontSendNotification);
        return;
    }

    // 安全設計（仕様 3.7）: 出力の上限（dBFS）を測定前に必ず設定する
    const auto ceiling = ceilingEditor.getText().getDoubleValue();
    ctx.engine.setOutputCeilingDb ((float) ceiling);
    ctx.settings.setValue (settingsCeilingKey, ceiling);
    ctx.settings.saveIfNeeded();

    stepLevels.clear();
    stepSpl.clear();
    stepLevel = -40.0;
    stepPhase = Phase::settling;
    phaseDeadline = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (settleSeconds);
    testStartTime = juce::Time::getCurrentTime();

    ctx.engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
    ctx.engine.setNoiseLevelDb ((float) stepLevel);
    ctx.engine.startExcitation();
    ctx.engine.getMeasurement().startMeasurement();
    startButton.setButtonText (strings::limitStopButton);
    eeq::log::write (juce::String ("limit test: step test started, ceiling=") + juce::String (ceiling, 1));
}

void LimitTestTab::stopTest (bool automatic)
{
    stepPhase = Phase::idle;
    thdPhase = ThdPhase::idle;
    thermalRunning = false;
    ctx.engine.stopExcitation();
    ctx.engine.getMeasurement().stopMeasurement();
    startButton.setButtonText (strings::limitStartButton);
    thermalButton.setButtonText (strings::limitThermalButton);
    progressLabel.setText (automatic ? strings::limitAutoStopped : juce::String (u8"停止しました"),
                           juce::dontSendNotification);
    eeq::log::write (juce::String ("limit test: stopped (automatic=") + (automatic ? "1" : "0") + ")");
    updateSuggestion();
}

void LimitTestTab::startThdTest()
{
    if (! acknowledgeToggle.getToggleState())
        return;

    // 対数スイープを鳴らしてIRから高調波を分離する（仕様 3.7）
    thdLevel = ctx.engine.getNoiseLevelDb();
    const auto sampleRate = ctx.engine.getCurrentSampleRate();
    auto sweep = eeq::dsp::LogSweepGenerator::generate (sampleRate, 10.0, 20.0, 20000.0,
                                                        (float) juce::Decibels::decibelsToGain (thdLevel));
    ctx.engine.setSweep (std::move (sweep));
    ctx.engine.startExcitation();
    ctx.engine.getMeasurement().startMeasurement();

    thdPhase = ThdPhase::playing;
    thdDeadline = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (11.5);
    thdLabel.setText (strings::limitThdLevel + ": " + juce::String (thdLevel, 1) + juce::String (u8" dBFS — ") + strings::limitThdButton
                          + juce::String (u8" 実行中…"),
                      juce::dontSendNotification);
}

void LimitTestTab::startThermalTest()
{
    if (! acknowledgeToggle.getToggleState())
        return;

    const auto ceiling = ceilingEditor.getText().getDoubleValue();
    ctx.engine.setOutputCeilingDb ((float) ceiling);
    thermalLevel = ctx.engine.getNoiseLevelDb();
    thermalRunning = true;
    thermalStart = juce::Time::getCurrentTime();
    thermalDeadline = thermalStart + juce::RelativeTime::seconds (30.0);
    thermalHistory.clear();

    ctx.engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
    ctx.engine.setNoiseLevelDb ((float) thermalLevel);
    ctx.engine.startExcitation();
    thermalButton.setButtonText (strings::limitStopButton);
    eeq::log::write (juce::String ("limit test: thermal test started, level=") + juce::String (thermalLevel, 1));
}

void LimitTestTab::timerTick()
{
    // 各測定の進行（メッセージスレッドで時分割。仕様 2. の (4) に沿い、重い処理は測定後に行う）
    const auto now = juce::Time::getCurrentTime();

    // ---- レベルステップ ----
    if (stepPhase != Phase::idle)
    {
        const auto ceiling = ceilingEditor.getText().getDoubleValue();
        const auto stepDb = std::max (0.5, stepEditor.getText().getDoubleValue());
        const auto maxDuration = std::max (10.0, durationEditor.getText().getDoubleValue());

        // 測定時間の上限（仕様 3.7）
        if ((now - testStartTime).inSeconds() > maxDuration)
        {
            stopTest (false);
            return;
        }

        if (stepPhase == Phase::settling && now > phaseDeadline)
        {
            stepPhase = Phase::measuring;
            phaseDeadline = now + juce::RelativeTime::seconds (measureSeconds);
        }
        else if (stepPhase == Phase::measuring && now > phaseDeadline)
        {
            stepLevels.push_back (stepLevel);
            stepSpl.push_back (currentSplFast());
            stepGraph.levels = stepLevels;
            stepGraph.spl = stepSpl;

            // 圧縮量 = 出力の上昇分 − SPLの上昇分（仕様 3.7）。基準は最初のステップ
            const auto expected = stepLevel - stepLevels.front();
            const auto actual = stepSpl.back() - stepSpl.front();
            const auto compression = expected - actual;

            progressLabel.setText (strings::limitStepProgress + ": " + juce::String (stepLevels.size())
                                       + "   " + juce::String (stepLevel, 1) + " dBFS   SPL "
                                       + juce::String (stepSpl.back(), 1) + "   "
                                       + strings::limitCompressionLabel + " " + juce::String (compression, 2) + " dB",
                                   juce::dontSendNotification);

            // 圧縮が1 dB に達したら自動停止（仕様 3.7）
            if (compression >= compressionLimitDb)
            {
                stopTest (true);
                return;
            }

            stepLevel += stepDb;

            if (stepLevel > ceiling)
            {
                stopTest (false);
                return;
            }

            ctx.engine.setNoiseLevelDb ((float) stepLevel);
            stepPhase = Phase::settling;
            phaseDeadline = now + juce::RelativeTime::seconds (settleSeconds);
            updateSuggestion();
            stepGraph.repaint();
        }
    }

    // ---- THD ----
    if (thdPhase == ThdPhase::playing && now > thdDeadline)
    {
        thdPhase = ThdPhase::idle;
        ctx.engine.stopExcitation();
        ctx.engine.getMeasurement().stopMeasurement();

        // IRから高調波を分離する（Farina法。仕様 3.7）
        eeq::dsp::SpectrumFrame frame;
        ctx.engine.getMeasurement().copyLatestResult (frame);

        if (frame.averages > 0)
        {
            const auto ir = eeq::dsp::buildImpulseResponse (frame);
            const auto sampleRate = ir.sampleRate;
            const auto sweepSeconds = 10.0;
            const auto peak = ir.peakTimeSamples;

            auto windowRms = [&] (double centerSample, double widthMs)
            {
                const auto width = widthMs * sampleRate / 1000.0;
                double sum = 0.0;
                int count = 0;

                for (int i = (int) (centerSample - width); i <= (int) (centerSample + width); ++i)
                {
                    if (i < 0 || i >= (int) ir.samples.size())
                        continue;

                    sum += (double) ir.samples[(size_t) i] * (double) ir.samples[(size_t) i];
                    ++count;
                }

                return count > 0 ? std::sqrt (sum / count) : 0.0;
            };

            const auto fundamental = windowRms (peak, 1.0);
            double harmonicPower = 0.0;
            juce::String harmonicText;

            for (int k = 2; k <= 5; ++k)
            {
                // 対数スイープの k 次高調波は t·ln(k)/ln(f2/f1) だけ早く現れる
                const auto arrival = eeq::dsp::LogSweepGenerator::harmonicArrivalSeconds (sweepSeconds, 20.0, 20000.0, k);
                const auto harmonicPeak = peak - arrival * sampleRate;
                const auto rms = windowRms (harmonicPeak, 1.0);
                harmonicPower += rms * rms;

                const auto percent = fundamental > 0.0 ? 100.0 * rms / fundamental : 0.0;
                harmonicText += (k == 2 ? "" : "  ") + juce::String (k) + juce::String (u8"次: ") + juce::String (percent, 2) + "%";
            }

            const auto thd = fundamental > 0.0 ? 100.0 * std::sqrt (harmonicPower) / fundamental : 0.0;
            thdLabel.setText (strings::limitThdLevel + ": " + juce::String (thdLevel, 1) + " dBFS   THD: "
                                  + juce::String (thd, 2) + "%   (" + harmonicText + ")",
                              juce::dontSendNotification);

            // 歪みが急に増えるレベルも上限の目安に加える（仕様 3.7）。ここでは表示まで
            eeq::log::write (juce::String ("limit test: THD=") + juce::String (thd, 3) + "% at "
                             + juce::String (thdLevel, 1) + " dBFS");
        }
    }

    // ---- 熱圧縮 ----
    if (thermalRunning)
    {
        const auto elapsed = (now - thermalStart).inSeconds();
        thermalHistory.push_back ({ elapsed, currentSplFast() });
        thermalGraph.samples = thermalHistory;
        thermalGraph.repaint();

        if (now > thermalDeadline)
        {
            thermalRunning = false;
            thermalButton.setButtonText (strings::limitThermalButton);
            ctx.engine.stopExcitation();
            eeq::log::write ("limit test: thermal test finished");
        }
    }
}

void LimitTestTab::updateSuggestion()
{
    // 圧縮が1 dB に達する手前のレベルをリミッターしきい値の目安にする（仕様 3.7）
    hasSuggestion = false;
    suggestedLimiterDbfs = 0.0;

    for (size_t i = 1; i < stepLevels.size(); ++i)
    {
        const auto expected = stepLevels[i] - stepLevels.front();
        const auto actual = stepSpl[i] - stepSpl.front();
        const auto compression = expected - actual;

        if (compression >= compressionLimitDb)
        {
            // 手前のステップ（圧縮がまだ1 dB未満）を上限の目安にする
            suggestedLimiterDbfs = i >= 2 ? stepLevels[i - 1] : stepLevels[i];
            hasSuggestion = true;
            break;
        }
    }

    if (! hasSuggestion)
    {
        suggestionLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    // アンプゲインとスピーカーの定格から dBu / V でも表示（仕様 3.7。手入力値）
    const auto ampGain = ampGainEditor.getText().getDoubleValue();
    const auto maxW = speakerEditor.getText().getDoubleValue();
    const auto ohm = 8.0;

    // リミッターしきい値の目安: 出力がこの dBFS のときアンプ出力は (dBFS + ampGain) dBu 相当。
    // スピーカーの電力から電圧を逆算: V = sqrt(W·R) を最大出力とし、しきい値の dBu を換算する
    const auto thresholdDbu = suggestedLimiterDbfs + ampGain;
    const auto thresholdVolts = eeq::util::dbuToVolts (thresholdDbu);
    const auto maxVolts = std::sqrt (maxW * ohm);

    juce::String extra;

    if (maxVolts > 0.0 && thresholdVolts > 0.0)
    {
        const auto headroomDb = 20.0 * std::log10 (maxVolts / thresholdVolts);
        extra = juce::String (u8"（スピーカー最大入力までの余裕: ") + juce::String (headroomDb, 1) + " dB = "
              + juce::String (thresholdVolts, 2) + juce::String (u8" Vrms / 最大 ") + juce::String (maxVolts, 1) + juce::String (u8" Vrms）");
    }

    suggestionLabel.setText (strings::limitSuggestion + ": " + juce::String (suggestedLimiterDbfs, 1) + " dBFS  /  "
                                 + juce::String (thresholdDbu, 1) + " dBu  /  " + juce::String (thresholdVolts, 2)
                                 + " V  " + extra,
                             juce::dontSendNotification);
}

void LimitTestTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    // コヒーレンス低下の併記（仕様 3.7: 落ち始める帯域とレベル）
    auto framePtr = ctx.engine.getMeasurement().consumeLatestFrame();
    if (! framePtr)
        return;
    const eeq::dsp::SpectrumFrame& frame = *framePtr;

    if (frame.averages > 0 && ! frame.coherence.empty())
    {
        const auto binWidth = frame.sampleRate / (double) frame.fftSize;
        double worstCoherence = 1.0;
        double worstFreq = 0.0;

        for (int k = 1; k < (int) frame.coherence.size(); ++k)
        {
            const auto freq = (double) k * binWidth;

            if (freq < 40.0 || freq > 500.0)
                continue;

            if (frame.coherence[(size_t) k] < worstCoherence)
            {
                worstCoherence = frame.coherence[(size_t) k];
                worstFreq = freq;
            }
        }

        if (worstCoherence < 0.8)
        {
            coherenceNoteLabel.setText (juce::String (u8"低域のコヒーレンス低下: 約 ")
                                            + juce::String (worstFreq, 0) + juce::String (u8" Hz で γ² ")
                                            + juce::String (worstCoherence, 2)
                                            + juce::String (u8"（歪みの兆候の可能性）"),
                                        juce::dontSendNotification);
        }
        else
        {
            // 回復したら警告を消す（消す経路がないと一度出た警告が残り続ける）
            coherenceNoteLabel.setText ({}, juce::dontSendNotification);
        }
    }
}

void LimitTestTab::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    title.setBounds (bounds.removeFromTop (20));

    auto top = bounds.removeFromTop (110);
    safetyTitle.setBounds (top.removeFromTop (18));
    warnLabel.setBounds (top.removeFromTop (18));

    auto safetyRow = top.removeFromTop (26);
    acknowledgeToggle.setBounds (safetyRow.removeFromLeft (200));
    safetyRow.removeFromLeft (8);
    ceilingLabel.setBounds (safetyRow.removeFromLeft (120));
    ceilingEditor.setBounds (safetyRow.removeFromLeft (60));
    safetyRow.removeFromLeft (8);
    stepLabel.setBounds (safetyRow.removeFromLeft (100));
    stepEditor.setBounds (safetyRow.removeFromLeft (50));
    safetyRow.removeFromLeft (8);
    durationLabel.setBounds (safetyRow.removeFromLeft (130));
    durationEditor.setBounds (safetyRow.removeFromLeft (50));

    auto buttonsRow = top;
    startButton.setBounds (buttonsRow.removeFromLeft (200));
    buttonsRow.removeFromLeft (8);
    thdButton.setBounds (buttonsRow.removeFromLeft (170));
    buttonsRow.removeFromLeft (8);
    thermalButton.setBounds (buttonsRow.removeFromLeft (180));

    auto graphRow = bounds.removeFromTop (200);
    stepGraph.setBounds (graphRow.removeFromLeft (graphRow.getWidth() / 2));
    graphRow.removeFromLeft (8);
    thermalGraph.setBounds (graphRow);

    bounds.removeFromTop (6);
    progressLabel.setBounds (bounds.removeFromTop (22));
    thdLabel.setBounds (bounds.removeFromTop (22));
    coherenceNoteLabel.setBounds (bounds.removeFromTop (20));

    bounds.removeFromTop (4);
    suggestionLabel.setBounds (bounds.removeFromTop (24));

    auto inputRow = bounds;
    ampGainLabel.setBounds (inputRow.removeFromLeft (130));
    ampGainEditor.setBounds (inputRow.removeFromLeft (60));
    inputRow.removeFromLeft (10);
    speakerLabel.setBounds (inputRow.removeFromLeft (260));
    speakerEditor.setBounds (inputRow.removeFromLeft (60));
}

} // namespace eeq::ui
