#include <utility>
#include "LimitTestTab.h"

#include "../Core/Log.h"
#include "../Core/Utilities.h"
#include "../DSP/CoherenceSummary.h"
#include "../DSP/SweepGenerator.h"
#include "../DSP/ThdAnalyzer.h"
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
    bool pathStarted = false; // Path::isEmpty() は始点だけのパスも空とみなす

    for (size_t i = 0; i < levels.size(); ++i)
    {
        const auto x = xOf (levels[i]);
        const auto y = yOf (spl[i]);

        if (! std::exchange (pathStarted, true))
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (theme::get (theme::ColourId::accent));
    g.strokePath (path, juce::PathStrokeType (1.6f));

    // 横軸ラベル
    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (juce::String (juce::roundToInt (levelMin)) + " dBFS", (int) area.getX(), (int) area.getBottom() + 2, 70, 12,
                juce::Justification::centredLeft);
    g.drawText (juce::String (juce::roundToInt (levelMax)) + " dBFS", (int) area.getRight() - 70, (int) area.getBottom() + 2, 70, 12,
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

    bool pathStarted = false; // Path::isEmpty() は始点だけのパスも空とみなす

    for (const auto& sample : samples)
    {
        const auto x = area.getX() + (float) (sample.first / std::max (1.0e-9, tMax)) * area.getWidth();
        const auto y = area.getBottom() - (float) ((sample.second - splMin) / (splMax - splMin)) * area.getHeight();

        if (! std::exchange (pathStarted, true))
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (theme::get (theme::ColourId::accent2));
    g.strokePath (path, juce::PathStrokeType (1.6f));

    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (strings::limitThermalDrop + ": " + juce::String (samples.front().second - samples.back().second, 2)
                    + " dB (" + juce::String (juce::roundToInt (tMax)) + "s)",
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
    styleLabelWithColour (warnLabel, theme::ColourId::danger, 12.5f, true);
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
        if (stepPhase != Phase::idle || thdPhase != ThdPhase::idle || thermalRunning)
            stopTest (false);
        else
            startStepTest();
    };
    addAndMakeVisible (startButton);

    styleButton (thdButton);
    thdButton.setButtonText (strings::limitThdButton);
    thdButton.onClick = [this]
    {
        if (thdPhase != ThdPhase::idle)
            stopTest (false);
        else
            startThdTest();
    };
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

    styleLabelWithColour (suggestionLabel, theme::ColourId::warn, 14.0f, true);
    addAndMakeVisible (suggestionLabel);

    addAndMakeVisible (stepGraph);
    addAndMakeVisible (thermalGraph);
}

LimitTestTab::~LimitTestTab()
{
    // テスト中に閉じられたら必ず止める（安全設計: 仕様 3.7）
    if (stepPhase != Phase::idle || thdPhase != ThdPhase::idle || thermalRunning)
    {
        ctx.engine.getMeasurement().stopMeasurement();
        ctx.engine.stopExcitation();
    }

    if (thdAnalysisThread.joinable())
        thdAnalysisThread.join();

    restoreCeilingAfterTest();
}

void LimitTestTab::overrideCeilingForTest()
{
    if (! ceilingOverridden)
    {
        ceilingBeforeTestDb = ctx.engine.getOutputCeilingDb();
        // 出力レベルもテスト前の値を退避して、終了時に必ず戻す（issue #100）
        levelBeforeTestDb = ctx.engine.getNoiseLevelDb();
        ceilingOverridden = true;
    }
}

std::optional<double> LimitTestTab::readCeilingInput()
{
    // 空欄・不正値・0 dBFS を超える値は受け付けない（issue #103: 空欄が 0 dBFS、"+6" が上限なしになっていた）
    const auto text = ceilingEditor.getText().trim();
    const auto value = text.getDoubleValue();

    if (text.isEmpty() || ! text.containsAnyOf ("0123456789") || ! std::isfinite (value) || value > 0.0 || value < -60.0)
    {
        progressLabel.setText (strings::limitCeilingInvalid, juce::dontSendNotification);
        return std::nullopt;
    }

    return value;
}

void LimitTestTab::restoreCeilingAfterTest()
{
    if (ceilingOverridden)
    {
        ctx.engine.setOutputCeilingDb (ceilingBeforeTestDb);
        ctx.engine.setNoiseLevelDb (levelBeforeTestDb);
        ceilingOverridden = false;
        eeq::log::write ("limit test: output level restored to " + juce::String (levelBeforeTestDb, 1) + " dBFS");
        eeq::log::write ("limit test: output ceiling restored to "
                         + juce::String (ceilingBeforeTestDb, 1) + " dBFS");
    }
}

std::optional<double> LimitTestTab::tryReadSpl() const
{
    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;

    if (ctx.engine.getSplAnalyzer().tryCopySpl (spl) && spl.hasSignal && std::isfinite (spl.currentDb))
        return spl.currentDb;

    return std::nullopt;
}

void LimitTestTab::updateCoherenceNote (const eeq::dsp::SpectrumFrame* frame)
{
    juce::String text;

    if (frame != nullptr)
    {
        const auto summary = eeq::dsp::summarizeLowBandCoherence (*frame);

        if (summary.hasData && summary.worstCoherence < 0.8f)
        {
            text = juce::String (u8"低域のコヒーレンス低下: 約 ")
                 + juce::String (juce::roundToInt (summary.worstFrequencyHz)) + juce::String (u8" Hz で γ² ")
                 + juce::String (summary.worstCoherence, 2)
                 + juce::String (u8"（歪みの兆候の可能性）");
        }
    }

    if (coherenceNoteLabel.getText() != text)
        coherenceNoteLabel.setText (text, juce::dontSendNotification);
}

void LimitTestTab::startStepTest()
{
    if (stepPhase != Phase::idle || thdPhase != ThdPhase::idle || thermalRunning)
        return;

    if (! acknowledgeToggle.getToggleState())
    {
        progressLabel.setText (strings::limitAcknowledge + juce::String (u8" にチェックしてください"), juce::dontSendNotification);
        return;
    }

    // 安全設計（仕様 3.7）: 出力の上限（dBFS）を測定前に必ず設定する。
    // テスト終了時に元の上限へ戻せるよう、テスト前の値を保存しておく（issue #29）
    const auto ceilingInput = readCeilingInput();

    if (! ceilingInput.has_value())
        return;

    const auto ceiling = *ceilingInput;
    overrideCeilingForTest();
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
    updateControlState();
    eeq::log::write (juce::String ("limit test: step test started, ceiling=") + juce::String (ceiling, 1));
}

void LimitTestTab::stopTest (bool automatic)
{
    ++thdGeneration; // 進行中のバックグラウンド解析結果を無効化
    stepPhase = Phase::idle;
    thdPhase = ThdPhase::idle;
    thermalRunning = false;
    ctx.engine.stopExcitation();
    ctx.engine.getMeasurement().stopMeasurement();
    restoreCeilingAfterTest();
    startButton.setButtonText (strings::limitStartButton);
    thdButton.setButtonText (strings::limitThdButton);
    thermalButton.setButtonText (strings::limitThermalButton);
    updateControlState();
    progressLabel.setText (automatic ? strings::limitAutoStopped : juce::String (u8"停止しました"),
                           juce::dontSendNotification);
    eeq::log::write (juce::String ("limit test: stopped (automatic=") + (automatic ? "1" : "0") + ")");
    updateSuggestion();
}

void LimitTestTab::startThdTest()
{
    if (stepPhase != Phase::idle || thdPhase != ThdPhase::idle || thermalRunning
        || thdAnalysisInFlight.load (std::memory_order_acquire)
        || ! acknowledgeToggle.getToggleState())
        return;

    // 対数スイープを鳴らしてIRから高調波を分離する（仕様 3.7）
    // ピーク1.0で生成（issue #29: レベルはオーディオスレッドがRMS基準で掛ける）
    thdLevel = ctx.engine.getNoiseLevelDb();
    thdSampleRate = ctx.engine.getCurrentSampleRate();
    auto sweep = eeq::dsp::LogSweepGenerator::generate (thdSampleRate, 10.0, 20.0, 20000.0, 1.0f);
    ctx.engine.setSweep (std::move (sweep));
    ctx.engine.startExcitation();
    ctx.engine.getMeasurement().startMeasurement();

    thdPhase = ThdPhase::playing;
    thdDeadline = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (11.5);
    thdButton.setButtonText (strings::limitStopButton);
    updateControlState();
    thdLabel.setText (strings::limitThdLevel + ": " + juce::String (thdLevel, 1) + juce::String (u8" dBFS — ") + strings::limitThdButton
                          + juce::String (u8" 実行中…"),
                      juce::dontSendNotification);
}

void LimitTestTab::startThermalTest()
{
    if (stepPhase != Phase::idle || thdPhase != ThdPhase::idle || thermalRunning
        || ! acknowledgeToggle.getToggleState())
        return;

    const auto ceilingInput = readCeilingInput();

    if (! ceilingInput.has_value())
        return;

    const auto ceiling = *ceilingInput;
    overrideCeilingForTest();
    ctx.engine.setOutputCeilingDb ((float) ceiling);
    // 熱圧縮テストのレベルも、ピークが天井でクリップしない範囲に抑える（issue #103）
    thermalLevel = juce::jmin ((double) ctx.engine.getNoiseLevelDb(), ceiling - eeq::audio::AudioEngine::excitationCrestDb);
    thermalRunning = true;
    thermalStart = juce::Time::getCurrentTime();
    thermalDeadline = thermalStart + juce::RelativeTime::seconds (30.0);
    thermalHistory.clear();

    ctx.engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
    ctx.engine.setNoiseLevelDb ((float) thermalLevel);
    ctx.engine.startExcitation();
    thermalButton.setButtonText (strings::limitStopButton);
    updateControlState();
    eeq::log::write (juce::String ("limit test: thermal test started, level=") + juce::String (thermalLevel, 1));
}

void LimitTestTab::updateControlState()
{
    const auto thdActive = thdPhase != ThdPhase::idle;
    const auto stepActive = stepPhase != Phase::idle;
    const auto analysisActive = thdAnalysisInFlight.load (std::memory_order_acquire);

    // Keep only the active test's stop control available; no other test may
    // replace its excitation or measurement while it is running.
    startButton.setEnabled (! thdActive && ! analysisActive);
    thermalButton.setEnabled (! thdActive && ! analysisActive && (! stepActive || thermalRunning));
    thdButton.setEnabled (thdActive || (! analysisActive && ! stepActive && ! thermalRunning));
}

void LimitTestTab::timerTick()
{
    // 各測定の進行（メッセージスレッドで時分割。仕様 2. の (4) に沿い、重い処理は測定後に行う）
    const auto now = juce::Time::getCurrentTime();

    // 新しい解析フレームの到着時だけ要約する。paint()は描画だけに保つ（issue #61）。
    auto frame = ctx.engine.getMeasurement().getLatestFrame();

    if (frame != lastCoherenceFrame)
    {
        lastCoherenceFrame = frame;
        updateCoherenceNote (frame.get());
    }

    // ---- レベルステップ ----
    if (stepPhase != Phase::idle)
    {
        const auto ceiling = (double) ctx.engine.getOutputCeilingDb(); // 開始時に検証・設定した値
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
            const auto spl = tryReadSpl();
            if (! spl)
                return;

            stepLevels.push_back (stepLevel);
            stepSpl.push_back (*spl);
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

            // RMS で上げるので、ピーク（RMS + クレスト約 14 dB）が天井を超える手前で止める（issue #103:
            // 以前は RMS が ceiling に届くまで上げ、最後の数ステップはハードクリップしたノイズになっていた）
            if (stepLevel > ceiling - eeq::audio::AudioEngine::excitationCrestDb)
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
        thdPhase = ThdPhase::waiting;
        ctx.engine.stopExcitation();
        ctx.engine.getMeasurement().stopMeasurement();
        const auto sampleRate = thdSampleRate;
        const auto captureSamples = (int) std::llround (11.5 * std::max (8000.0, sampleRate));
        auto* inFlight = &thdAnalysisInFlight;
        const auto level = thdLevel;
        const auto generation = ++thdGeneration;
        juce::Component::SafePointer<LimitTestTab> safeThis { this };
        try
        {
            // Freeze the window at stop; the rolling history keeps advancing during analysis.
            std::vector<float> recordedRef, recordedMeas;
            const auto copied = ctx.engine.getMeasurement().copyRecentSamplesExact (
                captureSamples, recordedRef, recordedMeas);

            if (thdAnalysisThread.joinable())
                thdAnalysisThread.join();

            thdAnalysisInFlight.store (true, std::memory_order_release);
            updateControlState();
            thdLabel.setText (strings::limitThdLevel + ": " + juce::String (level, 1)
                                  + juce::String (u8" dBFS — 解析中…"),
                              juce::dontSendNotification);

            thdAnalysisThread = std::thread ([safeThis, inFlight, copied, sampleRate, level, generation,
                                               recordedRef = std::move (recordedRef),
                                               recordedMeas = std::move (recordedMeas)]
            {
                float refPeak = 0.0f;

                for (const auto sample : recordedRef)
                    refPeak = std::max (refPeak, std::abs (sample));

                const auto referenceCaptured = copied && refPeak >= 1.0e-4f;
                eeq::dsp::ThdResult result;

                if (referenceCaptured)
                    result = eeq::dsp::analyzeThdFromSweepRecording (recordedRef, recordedMeas,
                                                                     sampleRate, 10.0, 20.0, 20000.0);

                inFlight->store (false, std::memory_order_release);
                juce::MessageManager::callAsync ([safeThis, generation, level, referenceCaptured,
                                                   result = std::move (result)] () mutable
                {
                    if (safeThis == nullptr)
                        return;

                    if (safeThis->thdGeneration != generation)
                    {
                        safeThis->updateControlState();
                        return;
                    }

                    safeThis->thdPhase = ThdPhase::idle;
                    safeThis->thdButton.setButtonText (strings::limitThdButton);
                    safeThis->updateControlState();

                    if (! referenceCaptured)
                    {
                        safeThis->thdLabel.setText (
                            juce::String (u8"基準信号（Ref）が録音できていません。内部参照またはRef入力の割り当てを確認してください"),
                            juce::dontSendNotification);
                        eeq::log::write ("limit test: THD FAILED (reference not captured)");
                        return;
                    }

                    if (! result.valid)
                    {
                        safeThis->thdLabel.setText (strings::limitThdLevel + ": " + juce::String (level, 1)
                                                        + juce::String (u8" dBFS   ") + result.failureReason,
                                                    juce::dontSendNotification);
                        eeq::log::write ("limit test: THD FAILED: " + result.failureReason);
                        return;
                    }

                    juce::String harmonicText;

                    for (int k = 2; k <= 5; ++k)
                        harmonicText += (k == 2 ? "" : "  ") + juce::String (k) + juce::String (u8"次: ")
                                      + juce::String (result.harmonicPercent[(size_t) (k - 2)], 2) + "%";

                    // 周波数ごとの THD（issue #114）: 代表値は最も歪んだ帯域。低域・中域・高域の値も並べる
                    juce::String bandText;

                    for (const auto target : { 63.0, 250.0, 1000.0, 4000.0 })
                    {
                        const eeq::dsp::ThdBand* nearest = nullptr;

                        for (const auto& band : result.bands)
                            if (nearest == nullptr || std::abs (std::log (band.centerHz / target))
                                                          < std::abs (std::log (nearest->centerHz / target)))
                                nearest = &band;

                        if (nearest != nullptr && std::abs (std::log2 (nearest->centerHz / target)) < 0.5)
                            bandText << "  " << juce::String (juce::roundToInt (nearest->nominalHz)) << " Hz: "
                                     << juce::String (nearest->thdPercent, 2) << "%";
                    }

                    safeThis->thdLabel.setText (strings::limitThdLevel + ": " + juce::String (level, 1)
                                                    + " dBFS   THD: " + juce::String (result.thdPercent, 2)
                                                    + "% @ " + juce::String (juce::roundToInt (result.worstFrequencyHz)) + " Hz   ("
                                                    + harmonicText + ")" + (bandText.isNotEmpty() ? "\n" + bandText.trim() : juce::String()),
                                                juce::dontSendNotification);
                    eeq::log::write (juce::String ("limit test: THD=") + juce::String (result.thdPercent, 3)
                                     + "% at " + juce::String (level, 1) + " dBFS");
                });
            });
        }
        catch (...)
        {
            thdAnalysisInFlight.store (false, std::memory_order_release);
            thdPhase = ThdPhase::idle;
            thdButton.setButtonText (strings::limitThdButton);
            thdLabel.setText (juce::String (u8"THD解析用スレッドを開始できませんでした"),
                              juce::dontSendNotification);
            updateControlState();
        }
    }

    // ---- 熱圧縮 ----
    if (thermalRunning)
    {
        const auto elapsed = (now - thermalStart).inSeconds();
        if (const auto spl = tryReadSpl())
        {
            thermalHistory.push_back ({ elapsed, *spl });
            thermalGraph.samples = thermalHistory;
            thermalGraph.repaint();
        }

        if (now > thermalDeadline)
        {
            thermalRunning = false;
            thermalButton.setButtonText (strings::limitThermalButton);
            ctx.engine.stopExcitation();
            updateControlState();
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
