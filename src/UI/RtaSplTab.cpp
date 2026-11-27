#include "RtaSplTab.h"

#include "../Core/Log.h"
#include "../Core/Utilities.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

namespace
{
constexpr const char* settingsWeightingKey = "splWeightingId";
constexpr const char* settingsTimeConstantKey = "splTimeConstantId";
constexpr const char* settingsBandsKey = "rtaBandsId";
constexpr const char* settingsAvgTimeKey = "rtaAvgTimeId";
constexpr const char* settingsOffsetKey = "splCalibrationOffsetDb";
constexpr const char* settingsTargetSplKey = "gainAssistTargetSpl";
constexpr const char* settingsFloorKey = "splNoiseFloorDb";
constexpr const char* settingsFloorCentersKey = "splNoiseFloorCenters";

juce::StringArray makeAvgTimeItems()
{
    return { "0.2 s", "0.5 s", "1 s", "2 s", "5 s", "10 s", "30 s" };
}

double avgTimeSecondsForId (int id)
{
    static const double values[] = { 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0 };
    return (id >= 1 && id <= 7) ? values[id - 1] : 1.0;
}
} // namespace

RtaSplTab::RtaSplTab (UiContext& context)
    : ctx (context)
{
    // ---- RTA設定 ----
    bandsTitle.setText (strings::rtaBandsLabel, juce::dontSendNotification);
    styleLabel (bandsTitle);
    addAndMakeVisible (bandsTitle);

    bandsCombo.addItem ("1/3 oct", 1);
    bandsCombo.addItem ("1/6 oct", 2);
    bandsCombo.addItem ("1/12 oct", 3);
    styleCombo (bandsCombo);
    addAndMakeVisible (bandsCombo);

    avgTimeTitle.setText (strings::rtaAvgTimeLabel, juce::dontSendNotification);
    styleLabel (avgTimeTitle);
    addAndMakeVisible (avgTimeTitle);

    for (const auto& item : makeAvgTimeItems())
        avgTimeCombo.addItem (item, avgTimeCombo.getNumItems() + 1);

    styleCombo (avgTimeCombo);
    addAndMakeVisible (avgTimeCombo);

    styleButton (peakHoldResetButton);
    peakHoldResetButton.setButtonText (strings::rtaPeakHoldButton);
    peakHoldResetButton.onClick = [this] { ctx.engine.getSplAnalyzer().resetPeakHold(); };
    addAndMakeVisible (peakHoldResetButton);

    styleButton (splResetButton);
    splResetButton.setButtonText (strings::splResetButton);
    splResetButton.onClick = [this] { ctx.engine.getSplAnalyzer().resetSpl(); };
    addAndMakeVisible (splResetButton);

    // ---- SPL ----
    splTitle.setText (strings::splSection, juce::dontSendNotification);
    styleLabel (splTitle, 13.0f, false);
    addAndMakeVisible (splTitle);

    splValueLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    splValueLabel.setFont (juce::Font (juce::FontOptions (34.0f, juce::Font::bold)));
    addAndMakeVisible (splValueLabel);

    leqLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (leqLabel);
    peakLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (peakLabel);

    weightingCombo.addItem ("Z", 1);
    weightingCombo.addItem ("C", 2);
    weightingCombo.addItem ("A", 3);
    weightingCombo.setSelectedId (3, juce::dontSendNotification); // 既定 A（仕様 3.4）
    styleCombo (weightingCombo);
    addAndMakeVisible (weightingCombo);

    timeConstantCombo.addItem (strings::splFast, 1);
    timeConstantCombo.addItem (strings::splSlow, 2);
    timeConstantCombo.setSelectedId (1, juce::dontSendNotification);
    styleCombo (timeConstantCombo);
    addAndMakeVisible (timeConstantCombo);

    uncalibratedLabel.setText (strings::splUncalibrated, juce::dontSendNotification);
    styleLabelWithColour (uncalibratedLabel, theme::ColourId::warn, 11.0f);
    addAndMakeVisible (uncalibratedLabel);

    // ---- S/N（仕様 3.5.1）----
    snTitle.setText (strings::snrSection, juce::dontSendNotification);
    styleLabel (snTitle, 13.0f, false);
    addAndMakeVisible (snTitle);

    styleButton (noiseFloorButton);
    noiseFloorButton.setButtonText (strings::snrNoiseFloorButton);
    noiseFloorButton.onClick = [this]
    {
        if (noiseFloorMeasuring)
            stopNoiseFloorMeasurement();
        else
            startNoiseFloorMeasurement();
    };
    addAndMakeVisible (noiseFloorButton);

    snOverallLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (snOverallLabel);

    snHintLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (snHintLabel);

    // ---- 校正 ----
    calibrationTitle.setText (strings::calibrationSection, juce::dontSendNotification);
    styleLabel (calibrationTitle, 13.0f, false);
    addAndMakeVisible (calibrationTitle);

    calibrationGuideLabel.setText (juce::String (strings::calibrationComparatorGuide) + "\n"
                                       + strings::calibrationCalibratorGuide + "\n"
                                       + strings::calibrationAccuracyNote,
                                   juce::dontSendNotification);
    calibrationGuideLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    calibrationGuideLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    addAndMakeVisible (calibrationGuideLabel);

    styleCombo (calibrationSignalCombo);
    calibrationSignalCombo.addItem (strings::calibrationSignalBandNoise, 1);
    calibrationSignalCombo.addItem (strings::calibrationSignalTone, 2);
    calibrationSignalCombo.addItem (strings::calibrationSignalPink, 3);
    calibrationSignalCombo.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (calibrationSignalCombo);

    styleAccentButton (calibrationStartButton);
    calibrationStartButton.setButtonText (strings::startNoiseButton);
    calibrationStartButton.onClick = [this]
    {
        if (ctx.engine.isNoiseAudible())
        {
            ctx.engine.stopExcitation();
            calibrationStartButton.setButtonText (strings::startNoiseButton);
        }
        else
        {
            const auto id = calibrationSignalCombo.getSelectedId();
            ctx.engine.startCalibrationSignal (id == 2 ? eeq::audio::AudioEngine::CalibrationSignal::tone1k
                                             : id == 3 ? eeq::audio::AudioEngine::CalibrationSignal::pinkBroadband
                                                       : eeq::audio::AudioEngine::CalibrationSignal::bandNoise1k);
            calibrationStartButton.setButtonText (strings::stopNoiseButton);
        }
    };
    addAndMakeVisible (calibrationStartButton);

    calibrationMeterLabel.setText (strings::calibrationNoiseMeterLabel, juce::dontSendNotification);
    styleLabel (calibrationMeterLabel);
    addAndMakeVisible (calibrationMeterLabel);

    styleTextEditor (calibrationMeterEditor);
    calibrationMeterEditor.setInputRestrictions (8, "0123456789.");
    addAndMakeVisible (calibrationMeterEditor);

    styleButton (calibrationApplyButton);
    calibrationApplyButton.setButtonText (strings::calibrationApplyButton);
    calibrationApplyButton.onClick = [this] { applyCalibrationOffset(); };
    addAndMakeVisible (calibrationApplyButton);

    styleButton (calibrationResetButton);
    calibrationResetButton.setButtonText (strings::calibrationClearButton);
    calibrationResetButton.onClick = [this]
    {
        ctx.engine.getSplAnalyzer().setCalibrationOffsetDb (0.0);
        ctx.settings.setValue (settingsOffsetKey, 0.0);
        ctx.settings.saveIfNeeded();
        refreshFromSettings();
    };
    addAndMakeVisible (calibrationResetButton);

    calibrationOffsetLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (calibrationOffsetLabel);

    // ---- ゲインアシスト（仕様 3.6）----
    gainTitle.setText (strings::gainAssistSection, juce::dontSendNotification);
    styleLabel (gainTitle, 13.0f, false);
    addAndMakeVisible (gainTitle);

    gainTargetLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    gainTargetLabel.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    addAndMakeVisible (gainTargetLabel);

    styleTextEditor (gainTargetEditor);
    gainTargetEditor.setInputRestrictions (6, "0123456789.");
    gainTargetEditor.onTextChange = [this]
    {
        const auto target = gainTargetEditor.getText().getDoubleValue();

        if (target > 30.0 && target < 140.0)
        {
            ctx.settings.setValue (settingsTargetSplKey, target);
            ctx.settings.saveIfNeeded();
        }

        updateGainAssist();
    };
    addAndMakeVisible (gainTargetEditor);

    styleButton (gainStartButton);
    gainStartButton.setButtonText (strings::startNoiseButton);
    gainStartButton.onClick = [this]
    {
        if (ctx.engine.isNoiseAudible())
        {
            ctx.engine.stopExcitation();
            gainStartButton.setButtonText (strings::startNoiseButton);
        }
        else
        {
            ctx.engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
            ctx.engine.startExcitation();
            gainStartButton.setButtonText (strings::stopNoiseButton);
        }
    };
    addAndMakeVisible (gainStartButton);

    gainMessageLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    gainMessageLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (gainMessageLabel);

    gainMetersLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (gainMetersLabel);

    gainProceedLabel.setText (strings::gainAssistToCalibration, juce::dontSendNotification);
    gainProceedLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    addAndMakeVisible (gainProceedLabel);

    bandsCombo.onChange = [this] { applyAnalyzerConfig(); };
    avgTimeCombo.onChange = [this] { applyAnalyzerConfig(); };
    weightingCombo.onChange = [this]
    {
        const auto id = weightingCombo.getSelectedId();
        ctx.engine.getSplAnalyzer().setWeighting (id == 1 ? eeq::dsp::RtaSplAnalyzer::Weighting::zWeighted
                                                : id == 2 ? eeq::dsp::RtaSplAnalyzer::Weighting::cWeighted
                                                          : eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted);
        ctx.settings.setValue (settingsWeightingKey, id);
        ctx.settings.saveIfNeeded();
    };
    timeConstantCombo.onChange = [this]
    {
        ctx.engine.getSplAnalyzer().setTimeConstant (
            timeConstantCombo.getSelectedId() == 2 ? eeq::dsp::RtaSplAnalyzer::TimeConstant::slow
                                                   : eeq::dsp::RtaSplAnalyzer::TimeConstant::fast);
        ctx.settings.setValue (settingsTimeConstantKey, timeConstantCombo.getSelectedId());
        ctx.settings.saveIfNeeded();
    };

    refreshFromSettings();
    applyAnalyzerConfig();
}

void RtaSplTab::refreshFromSettings()
{
    weightingCombo.setSelectedId (juce::jlimit (1, 3, ctx.settings.getIntValue (settingsWeightingKey, 3)),
                                  juce::dontSendNotification);
    timeConstantCombo.setSelectedId (juce::jlimit (1, 2, ctx.settings.getIntValue (settingsTimeConstantKey, 1)),
                                     juce::dontSendNotification);
    bandsCombo.setSelectedId (juce::jlimit (1, 3, ctx.settings.getIntValue (settingsBandsKey, 1)),
                              juce::dontSendNotification);
    avgTimeCombo.setSelectedId (juce::jlimit (1, 7, ctx.settings.getIntValue (settingsAvgTimeKey, 3)),
                                juce::dontSendNotification);
    gainTargetEditor.setText (juce::String (ctx.settings.getDoubleValue (settingsTargetSplKey, 85.0), 0),
                              juce::dontSendNotification);

    // 校正オフセットを解析器へ復元する（仕様 3.4）
    ctx.engine.getSplAnalyzer().setCalibrationOffsetDb (ctx.settings.getDoubleValue (settingsOffsetKey, 0.0));

    // ノイズフロアの復元（仕様 3.5.1: 測って保存する）
    const auto centersVar = ctx.settings.getValue (settingsFloorCentersKey);
    const auto levelsVar = ctx.settings.getValue (settingsFloorKey);

    if (centersVar.isNotEmpty() && levelsVar.isNotEmpty())
    {
        auto readArray = [] (const juce::String& text)
        {
            std::vector<double> values;
            juce::StringArray tokens;
            tokens.addTokens (text, ",", "");

            for (const auto& token : tokens)
                values.push_back (token.getDoubleValue());

            return values;
        };

        floorCenters = readArray (centersVar);

        floorLevels.clear();

        for (const auto value : readArray (levelsVar))
            floorLevels.push_back ((float) value);
    }

    weightingCombo.onChange();
    timeConstantCombo.onChange();
    calibrationOffsetLabel.setText (strings::calibrationOffsetLabel + ": "
                                       + juce::String (ctx.engine.getSplAnalyzer().getCalibrationOffsetDb(), 1)
                                       + " dB",
                                    juce::dontSendNotification);
    // 「未校正」警告はオフセットが 0 のときだけ出す（校正済みなら消す）
    uncalibratedLabel.setVisible (std::abs (ctx.engine.getSplAnalyzer().getCalibrationOffsetDb()) < 1.0e-9);
}

void RtaSplTab::applyAnalyzerConfig()
{
    const auto bands = bandsCombo.getSelectedId() == 2 ? 6 : bandsCombo.getSelectedId() == 3 ? 12 : 3;
    configuredSampleRate = ctx.engine.getCurrentSampleRate();
    ctx.engine.getSplAnalyzer().configure (configuredSampleRate, bands,
                                           avgTimeSecondsForId (avgTimeCombo.getSelectedId()));
    ctx.settings.setValue (settingsBandsKey, bandsCombo.getSelectedId());
    ctx.settings.setValue (settingsAvgTimeKey, avgTimeCombo.getSelectedId());
    ctx.settings.saveIfNeeded();
}

void RtaSplTab::timerTick()
{
    auto& analyzer = ctx.engine.getSplAnalyzer();

    // デバイスが開き直った（サンプルレートが変わった）ときは、保存済みの
    // ユーザー設定で解析器を張り直す（ハードコードで上書きしない）
    if (std::abs (ctx.engine.getCurrentSampleRate() - configuredSampleRate) > 0.5)
        applyAnalyzerConfig();

    // RTA表示の上端（issue #49）: 校正オフセットは「0 dBFS が何 dB SPL か」なので、
    // 校正後の上端は offset そのもの（94 を足すと 94 dB 分高くなり全バーが見えなくなる）
    const auto offset = analyzer.getCalibrationOffsetDb();
    splTopDb = eeq::dsp::RtaSplAnalyzer::rtaScaleTopDb (offset);

    // RTAとSPLを1回のロックで取得する（issue #37）
    eeq::dsp::RtaSplAnalyzer::RtaFrame frame;
    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    if (! analyzer.tryCopyAll (frame, spl))
        return; // 解析中なら前回表示を維持し、UIスレッドは待たない
    rtaCenters = frame.centerFreq;
    rtaLevels = frame.levelDb;
    rtaPeakLevels = frame.peakHoldDb;
    lastSplDb = spl.currentDb;

    splValueLabel.setText (spl.hasSignal ? juce::String (spl.currentDb, 1) : "--",
                           juce::dontSendNotification);

    // 励起の開始/停止ボタン文言はエンジンの実状態に同期させる（下部バーなど
    // 他の経路で止められた場合に「停止」表示のまま残らないように）
    const auto noiseButtonText = ctx.engine.isNoiseAudible() ? strings::stopNoiseButton
                                                              : strings::startNoiseButton;
    calibrationStartButton.setButtonText (noiseButtonText);
    gainStartButton.setButtonText (noiseButtonText);

    leqLabel.setText (juce::String (u8"Leq  ") + strings::splLeq1min + ": " + juce::String (spl.leq1minDb, 1)
                          + "   " + strings::splLeq15min + ": " + juce::String (spl.leq15minDb, 1)
                          + "   " + strings::splLeqSession + ": " + juce::String (spl.leqSessionDb, 1),
                      juce::dontSendNotification);

    peakLabel.setText (strings::splPeakLabel + ": " + juce::String (spl.peakDb, 1) + " dB",
                       juce::dontSendNotification);

    // ノイズフロア測定（5秒間の平均をフロアとして保存。仕様 3.5.1）
    if (noiseFloorMeasuring && juce::Time::getCurrentTime() > noiseFloorDeadline)
    {
        noiseFloorMeasuring = false;
        floorCenters = rtaCenters;
        floorLevels = rtaLevels;
        noiseFloorButton.setButtonText (strings::snrNoiseFloorButton);

        // 設定に保存する
        juce::StringArray centerTokens, levelTokens;

        for (const auto center : floorCenters)
            centerTokens.add (juce::String (center, 2));

        for (const auto level : floorLevels)
            levelTokens.add (juce::String (level, 2));

        ctx.settings.setValue (settingsFloorCentersKey, centerTokens.joinIntoString (","));
        ctx.settings.setValue (settingsFloorKey, levelTokens.joinIntoString (","));
        ctx.settings.saveIfNeeded();
        eeq::log::write ("noise floor captured (" + juce::String (floorCenters.size()) + " bands)");
    }

    // S/N
    {
        juce::String snrText;
        double sumSignal = 0.0, sumFloor = 0.0;
        int validBands = 0;

        if (! floorCenters.empty())
        {
            for (size_t i = 0; i < rtaCenters.size(); ++i)
            {
                // ノイズフロアは同じ帯域数・同じ中心周波数のときだけ対応付ける
                //（フロア取得後にバンド数を変えたら無効とみなす）
                if (i < floorLevels.size() && i < floorCenters.size()
                    && floorCenters[i] > 0.0 && std::abs (floorCenters[i] - rtaCenters[i]) < 0.5
                    && floorLevels[i] > -200.0f && rtaLevels[i] > -200.0f)
                {
                    sumSignal += std::pow (10.0, rtaLevels[i] / 10.0);
                    sumFloor += std::pow (10.0, floorLevels[i] / 10.0);
                    ++validBands;
                }
            }
        }

        if (validBands > 0 && sumFloor > 0.0)
        {
            const auto overall = 10.0 * std::log10 (sumSignal / sumFloor);
            snrText = strings::snrOverall + ": " + juce::String (overall, 1) + " dB";

            // 色による判定の目安（仕様 3.5.1）: 20dB以上=緑 10〜20=黄 10未満=赤
            snOverallLabel.setColour (juce::Label::textColourId,
                                      overall >= 20.0 ? theme::get (theme::ColourId::ok)
                                       : overall >= 10.0 ? theme::get (theme::ColourId::warn)
                                                         : theme::get (theme::ColourId::danger));
        }
        else
        {
            snrText = strings::snrNoiseFloorHint;
        }

        snOverallLabel.setText (snrText, juce::dontSendNotification);
    }

    updateSnHint();
    updateGainAssist();

    // 校正オフセット計算（重み付け切替後のリセット完了待ち）
    processCalibrationPending();

    repaint();
}

void RtaSplTab::processCalibrationPending()
{
    // 校正待ちの進行（issue #55: タブ非表示でも MainComponent から呼ばれる）。
    // オフセットの計算は「既存オフセットを外した生の値」を基準にする（issue #48:
    // 表示値はすでにオフセット込みのため、そのまま差を取ると 2 回目で 0 に戻る）
    if (! calibrationOffsetPending)
        return;

    auto& analyzer = ctx.engine.getSplAnalyzer();
    eeq::dsp::RtaSplAnalyzer::SplSnapshot spl;
    if (! analyzer.tryCopySpl (spl))
        return; // calibration待ちもメッセージスレッドをブロックしない

    if (spl.hasSignal)
    {
        const auto previousOffset = analyzer.getCalibrationOffsetDb();
        const auto offset = eeq::dsp::RtaSplAnalyzer::calibrationOffsetFor (
            calibrationMeterValue, spl.currentDb, previousOffset);
        analyzer.setCalibrationOffsetDb (offset);
        uncalibratedLabel.setVisible (std::abs (offset) < 1.0e-9);
        ctx.settings.setValue (settingsOffsetKey, offset);
        ctx.settings.saveIfNeeded();

        calibrationOffsetLabel.setText (strings::calibrationOffsetLabel + ": " + juce::String (offset, 1) + " dB",
                                        juce::dontSendNotification);
        eeq::log::write (juce::String ("SPL calibration: meter=") + juce::String (calibrationMeterValue, 1)
                         + " app=" + juce::String (spl.currentDb, 1) + " (previous offset "
                         + juce::String (previousOffset, 2) + ") offset=" + juce::String (offset, 2));
        calibrationOffsetPending = false;
    }
    else if (juce::Time::getCurrentTime() > calibrationDeadline)
    {
        calibrationOffsetPending = false;
        calibrationOffsetLabel.setText (
            juce::String (u8"校正用の音を入力で検出できません。音源・入力ゲイン・接続を確認してください。"),
            juce::dontSendNotification);
    }
}

void RtaSplTab::timerTickBackground()
{
    // 非表示タブ用の軽量ティック（issue #55）: 校正オフセット待ちの進行だけを行う
    processCalibrationPending();
}

void RtaSplTab::startNoiseFloorMeasurement()
{
    noiseFloorMeasuring = true;
    noiseFloorDeadline = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5.0);
    noiseFloorButton.setButtonText (strings::snrNoiseFloorStop);
    eeq::log::write ("noise floor: measuring ambient noise for 5 seconds");
}

void RtaSplTab::stopNoiseFloorMeasurement()
{
    noiseFloorMeasuring = false;
    noiseFloorButton.setButtonText (strings::snrNoiseFloorButton);
}

void RtaSplTab::applyCalibrationOffset()
{
    // 比較校正: マイク入力の表示（dB(A) Fast）と騒音計の読みの差を保存する。
    // アプリ内信号だけでなく、校正器や外部音源の入力でも校正できる（仕様 3.4）。
    const auto meterValue = calibrationMeterEditor.getText().getDoubleValue();

    if (! std::isfinite (meterValue) || meterValue < 30.0 || meterValue > 140.0)
    {
        calibrationOffsetLabel.setText (juce::String (u8"騒音計の値は 30〜140 dB で入力してください"),
                                        juce::dontSendNotification);
        return;
    }

    auto& analyzer = ctx.engine.getSplAnalyzer();

    // 比較校正中はアプリ側の表示も dB(A) Fast に合わせる（仕様 3.4）。設定にも保存する
    analyzer.setWeighting (eeq::dsp::RtaSplAnalyzer::Weighting::aWeighted);
    analyzer.setTimeConstant (eeq::dsp::RtaSplAnalyzer::TimeConstant::fast);
    // 重み付け・時定数変更後は古い平均値をリセットしてから新しいフレームを待つ
    analyzer.resetSpl();
    weightingCombo.setSelectedId (3, juce::dontSendNotification);
    timeConstantCombo.setSelectedId (1, juce::dontSendNotification);
    ctx.settings.setValue (settingsWeightingKey, 3);
    ctx.settings.setValue (settingsTimeConstantKey, 1);

    // 少し待ってから新しいフレームで値を取る（タイマーで再取得するためここでは返す）
    // 実際のオフセット計算は次の timerTick で行う
    calibrationOffsetPending = true;
    calibrationMeterValue = meterValue;
    calibrationDeadline = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (2.0);
    calibrationOffsetLabel.setText (juce::String (u8"A/Fast の新しい測定値を取得しています…"),
                                    juce::dontSendNotification);
}

void RtaSplTab::updateGainAssist()
{
    // 目標SPLを大きく表示（仕様 3.6 (1)）
    const auto target = ctx.settings.getDoubleValue (settingsTargetSplKey, 85.0);
    gainTargetLabel.setText (juce::String (target, 0) + " dB(A) " + strings::splFast,
                             juce::dontSendNotification);

    const auto& meter = ctx.engine.getMeasMeter();
    const auto peak = meter.peakLinear.load (std::memory_order_relaxed);
    const auto rms = meter.rmsLinear.load (std::memory_order_relaxed);
    const auto peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    const auto rmsDb = juce::Decibels::gainToDecibels (rms, -100.0f);

    gainMetersLabel.setText (juce::String (strings::gainAssistPeak) + ": " + juce::String (peakDb, 1)
                                 + " dBFS   " + strings::gainAssistRms + ": " + juce::String (rmsDb, 1)
                                 + " dBFS   " + strings::gainAssistHeadroom + ": " + juce::String (-(peakDb), 1)
                                 + " dB   (Meas)",
                             juce::dontSendNotification);

    // 状態に応じたメッセージ（仕様 3.6 (4)）: ピーク −6 dBFS 以下、RMS −24〜−18 dBFS
    if (rmsDb < -60.0f)
    {
        gainMessageLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    if (peakDb > -6.0f)
    {
        gainMessageLabel.setText (strings::gainAssistLower + " / " + strings::gainAssistTooHot,
                                  juce::dontSendNotification);
        gainMessageLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::danger));
    }
    else if (rmsDb < -24.0f)
    {
        const auto delta = -21.0 - rmsDb;
        gainMessageLabel.setText (strings::gainAssistRaise + juce::String (delta, 0) + juce::String (u8" dB）"),
                                  juce::dontSendNotification);
        gainMessageLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::warn));
    }
    else if (rmsDb > -18.0f)
    {
        gainMessageLabel.setText (strings::gainAssistLower, juce::dontSendNotification);
        gainMessageLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::warn));
    }
    else
    {
        gainMessageLabel.setText (strings::gainAssistOk, juce::dontSendNotification);
        gainMessageLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::ok));
    }
}

void RtaSplTab::updateSnHint()
{
    // S/N とコヒーレンスを合わせて読むためのヒント（仕様 3.5.1）
    if (floorCenters.empty() || rtaCenters.empty())
    {
        snHintLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    bool lowSnr = false;
    bool lowCoherence = false;

    // ロックフリーでフレーム取得（コヒーレンスのみ参照）
    auto framePtr = ctx.engine.getMeasurement().getLatestFrame();
    if (! framePtr)
        return;
    const eeq::dsp::SpectrumFrame& frame = *framePtr;

    for (size_t i = 0; i < rtaCenters.size() && i < floorLevels.size() && i < floorCenters.size(); ++i)
    {
        // フロアは同じ帯域のときだけ対応付ける（バンド数を変えたら無効）
        if (floorCenters[i] <= 0.0 || std::abs (floorCenters[i] - rtaCenters[i]) >= 0.5)
            continue;

        if (rtaLevels[i] < -200.0f || floorLevels[i] < -200.0f)
            continue;

        if (rtaCenters[i] < 100.0 || rtaCenters[i] > 10000.0)
            continue;

        const auto snr = rtaLevels[i] - floorLevels[i];

        if (snr < 10.0)
            lowSnr = true;

        // コヒーレンスはTF測定から近傍binを拾う
        if (frame.averages > 0 && ! frame.coherence.empty())
        {
            const auto binWidth = frame.sampleRate / (double) frame.fftSize;
            const auto k = juce::jlimit (1, (int) frame.coherence.size() - 1,
                                         (int) (rtaCenters[i] / binWidth));

            if (frame.coherence[(size_t) k] < 0.6f && snr >= 20.0)
                lowCoherence = true;
        }
    }

    if (lowSnr)
        snHintLabel.setText (strings::snrHintNoise, juce::dontSendNotification);
    else if (lowCoherence)
        snHintLabel.setText (strings::snrHintReflection, juce::dontSendNotification);
    else
        snHintLabel.setText ({}, juce::dontSendNotification);
}

void RtaSplTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    // ---- RTAバー ----
    // 描画領域は resized() の左カラム（設定行の下）と一致させる。全幅・最上段で
    // 描くと右カラムのパネルや S/N ラベルの背面にバーが重なる
    auto paintBounds = getLocalBounds().reduced (8);
    auto leftColumn = paintBounds.removeFromLeft (paintBounds.getWidth() * 3 / 5);
    leftColumn.removeFromTop (26 + 4); // 設定行 + 間隔
    const auto rtaArea = leftColumn.removeFromTop (rtaGraphHeight).toFloat();

    g.setColour (theme::get (theme::ColourId::graphBackground));
    g.fillRoundedRectangle (rtaArea, 6.0f);

    if (rtaCenters.empty())
    {
        g.setColour (theme::get (theme::ColourId::textDim));
        g.drawText (strings::graphPlaceholder, rtaArea.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto count = (int) rtaCenters.size();
    const auto plotArea = rtaArea.withTrimmedBottom (16.0f);
    const auto barWidth = plotArea.getWidth() / (float) count;
    const auto rangeDb = 90.0f; // 90 dB幅で表示

    // S/N が保存されている帯域は背景に色を付ける（仕様 3.5.1）
    if (! floorLevels.empty())
    {
        for (int i = 0; i < count && i < (int) floorLevels.size(); ++i)
        {
            if (rtaLevels[(size_t) i] < -200.0f || floorLevels[(size_t) i] < -200.0f)
                continue;

            const auto snr = rtaLevels[(size_t) i] - floorLevels[(size_t) i];
            const auto colour = snr >= 20.0f ? theme::get (theme::ColourId::ok).withAlpha (0.10f)
                              : snr >= 10.0f ? theme::get (theme::ColourId::warn).withAlpha (0.10f)
                                             : theme::get (theme::ColourId::danger).withAlpha (0.12f);

            g.setColour (colour);
            g.fillRect (plotArea.getX() + (float) i * barWidth, plotArea.getY(), barWidth, plotArea.getHeight());
        }
    }

    bool anySignal = false;
    for (int i = 0; i < count; ++i)
    {
        const auto level = rtaLevels[(size_t) i];

        if (level < -200.0f)
            continue;

        anySignal = true;
        const auto normalized = juce::jlimit (0.0f, 1.0f, (level - (splTopDb - rangeDb)) / rangeDb);
        const auto h = normalized * plotArea.getHeight();
        const auto x = plotArea.getX() + (float) i * barWidth;

        g.setColour (theme::get (theme::ColourId::accent).withAlpha (0.75f));
        g.fillRect (x + 1.0f, plotArea.getBottom() - h, std::max (1.0f, barWidth - 2.0f), h);

        // ピークホールド（仕様 3.4）
        const auto peak = rtaPeakLevels[(size_t) i];

        if (peak > -200.0f)
        {
            const auto peakNorm = juce::jlimit (0.0f, 1.0f, (peak - (splTopDb - rangeDb)) / rangeDb);
            const auto py = plotArea.getBottom() - peakNorm * plotArea.getHeight();
            g.setColour (theme::get (theme::ColourId::warn));
            g.fillRect (x + 1.0f, py - 1.5f, std::max (1.0f, barWidth - 2.0f), 1.5f);
        }
    }

    // 無音時の表示（OS音量0やマイク未接続等で入力がない場合）
    if (! anySignal)
    {
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (strings::rtaNoSignal, plotArea, juce::Justification::centred);
    }

    // バンドのラベル（主要な1/3オクターブ中心だけ）
    g.setColour (theme::get (theme::ColourId::gridText));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    for (int i = 0; i < count; ++i)
    {
        const auto f = rtaCenters[(size_t) i];
        const auto stepsFrom20 = std::log2 (f / 20.0) * 3.0;

        if (std::abs (stepsFrom20 - std::round (stepsFrom20)) >= 0.05)
            continue;

        const auto text = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        const auto x = plotArea.getX() + ((float) i + 0.5f) * barWidth;
        g.drawText (text, (int) x - 22, (int) plotArea.getBottom() + 2, 44, 12, juce::Justification::centred);
    }
    // dB目盛りと水平グリッド（issue #27）: 上端 splTopDb から 10 dB 刻み。
    // 未校正（0 dBFS 基準）と校正後（94 dB SPL 基準）のどちらのスケールかラベルで明示する
    g.setFont (juce::Font (juce::FontOptions (9.5f)));

    for (auto db = splTopDb; db >= splTopDb - rangeDb; db -= 10.0f)
    {
        const auto y = plotArea.getBottom ()
                     - (db - (splTopDb - rangeDb)) / rangeDb * plotArea.getHeight ();

        if (y < plotArea.getY () - 0.5f || y > plotArea.getBottom () + 0.5f)
            continue;

        const auto isTop = std::abs (db - splTopDb) < 0.01f;
        g.setColour (theme::get (theme::ColourId::grid).withAlpha (isTop ? 0.9f : 0.45f));
        g.drawLine (plotArea.getX (), y, plotArea.getRight (), y, isTop ? 1.2f : 1.0f);

        g.setColour (theme::get (theme::ColourId::gridText));
        g.drawText (juce::String (db, 0), (int) plotArea.getX () - 34, (int) y - 6, 30, 12,
                    juce::Justification::centredRight);
    }

    g.setColour (theme::get (theme::ColourId::gridText));
    g.drawText (std::abs (splTopDb) < 0.01f ? juce::String (u8"dBFS（未校正）")
                                            : juce::String (u8"dB SPL（校正済み）"),
                (int) plotArea.getX () - 40, (int) plotArea.getY () - 18, 150, 12,
                juce::Justification::centredLeft);
}


void RtaSplTab::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    rtaGraphHeight = juce::jlimit (160, 320, bounds.getHeight() * 3 / 5);
    auto graphColumn = bounds.removeFromLeft (bounds.getWidth() * 3 / 5);
    auto rightColumn = bounds;

    // 左: RTA設定行 + グラフ + S/N
    // リセット系ボタンは右端から固定幅で確保し、最小幅でも潰れないようにする（issue #28）
    auto configRow = graphColumn.removeFromTop (26);
    splResetButton.setBounds (configRow.removeFromRight (110));
    configRow.removeFromRight (8);
    peakHoldResetButton.setBounds (configRow.removeFromRight (150));
    configRow.removeFromRight (8);
    avgTimeCombo.setBounds (configRow.removeFromRight (70));
    avgTimeTitle.setBounds (configRow.removeFromRight (70));
    configRow.removeFromRight (8);
    bandsCombo.setBounds (configRow.removeFromRight (90));
    bandsTitle.setBounds (configRow);

    graphColumn.removeFromTop (4);
    graphColumn.removeFromTop (rtaGraphHeight); // グラフ描画領域（paint がここに描く）

    // S/Nはグラフの下に置く
    auto snArea = graphColumn;
    snTitle.setBounds (snArea.removeFromTop (18));
    snOverallLabel.setBounds (snArea.removeFromTop (20));
    snHintLabel.setBounds (snArea.removeFromTop (18));
    noiseFloorButton.setBounds (snArea.removeFromTop (26).removeFromLeft (150));

    // 右: SPL / 校正 / ゲインアシスト
    auto splPanel = rightColumn.removeFromTop (140);
    splTitle.setBounds (splPanel.removeFromTop (18));
    splValueLabel.setBounds (splPanel.removeFromTop (40));
    splValueLabel.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));

    auto splControls = splPanel.removeFromTop (26);
    weightingCombo.setBounds (splControls.removeFromLeft (64));
    timeConstantCombo.setBounds (splControls.removeFromLeft (76));
    uncalibratedLabel.setBounds (splPanel.removeFromTop (16));
    leqLabel.setBounds (splPanel.removeFromTop (18));
    peakLabel.setBounds (splPanel);

    auto calibrationPanel = rightColumn.removeFromTop (150);
    calibrationTitle.setBounds (calibrationPanel.removeFromTop (18));
    auto calRow1 = calibrationPanel.removeFromTop (26);
    calibrationSignalCombo.setBounds (calRow1.removeFromLeft (170));
    calRow1.removeFromLeft (6);
    calibrationStartButton.setBounds (calRow1.removeFromLeft (100));
    auto calRow2 = calibrationPanel.removeFromTop (26);
    calibrationMeterLabel.setBounds (calRow2.removeFromLeft (140));
    calibrationMeterEditor.setBounds (calRow2.removeFromLeft (70));
    calRow2.removeFromLeft (6);
    calibrationApplyButton.setBounds (calRow2.removeFromLeft (120));
    calibrationOffsetLabel.setBounds (calRow2);
    auto calRow3 = calibrationPanel.removeFromTop (26);
    calibrationResetButton.setBounds (calRow3.removeFromLeft (120));
    calibrationPanel.removeFromTop (2);
    calibrationGuideLabel.setBounds (calibrationPanel);

    auto gainPanel = rightColumn;
    gainTitle.setBounds (gainPanel.removeFromTop (18));
    auto gainRow1 = gainPanel.removeFromTop (30);
    gainTargetLabel.setBounds (gainRow1.removeFromLeft (170));
    gainRow1.removeFromLeft (6);
    gainTargetEditor.setBounds (gainRow1.removeFromLeft (60));
    gainRow1.removeFromLeft (6);
    gainStartButton.setBounds (gainRow1.removeFromLeft (110));
    gainMessageLabel.setBounds (gainPanel.removeFromTop (22));
    gainMetersLabel.setBounds (gainPanel.removeFromTop (18));
    gainProceedLabel.setBounds (gainPanel);

}
} // namespace eeq::ui
