#include "BottomBar.h"

#include "../Core/Log.h"
#include "../DSP/SweepGenerator.h"
#include "Buttons.h"
#include "Strings.h"

namespace eeq::ui
{

BottomBar::BottomBar (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn)
{
    styleAccentButton (measurementButton);
    measurementButton.setButtonText (strings::measurementStartButton);
    measurementButton.onClick = [this]
    {
        auto& measurement = engine.getMeasurement();

        if (measurement.isMeasuring())
            measurement.stopMeasurement();
        else
            measurement.startMeasurement();
    };
    addAndMakeVisible (measurementButton);

    styleButton (resetButton);
    resetButton.setButtonText (strings::resetAveragesButton);
    resetButton.onClick = [this]
    {
        engine.getMeasurement().resetAverages();
        engine.clearClipLatches();
    };
    addAndMakeVisible (resetButton);

    styleButton (snapshotButton);
    snapshotButton.setButtonText (strings::snapshotButton);
    snapshotButton.onClick = [this]
    {
        if (onSnapshotRequested != nullptr)
            onSnapshotRequested();
    };
    addAndMakeVisible (snapshotButton);

    styleButton (delayButton);
    delayButton.setButtonText (strings::delayMeasureButton);
    delayButton.onClick = [this]
    {
        if (onDelayMeasureRequested != nullptr)
            onDelayMeasureRequested();
    };
    addAndMakeVisible (delayButton);

    styleButton (calibrationButton);
    calibrationButton.setButtonText (strings::calibrationButton);
    calibrationButton.onClick = [this]
    {
        if (onCalibrationRequested != nullptr)
            onCalibrationRequested();
    };
    addAndMakeVisible (calibrationButton);

    // 励起信号（仕様 3.2）: ピンクノイズ（既定）/ スイープ / WAV / 外部入力のみ
    styleCombo (excitationCombo);
    excitationCombo.addItem (strings::excitationPinkNoise, 1);
    excitationCombo.addItem (strings::excitationSweep, 2);
    excitationCombo.addItem (strings::excitationWav, 3);
    excitationCombo.addItem (strings::excitationExternal, 4);
    excitationCombo.setSelectedId (1, juce::dontSendNotification);
    excitationCombo.onChange = [this] { applyExcitationSource (excitationCombo.getSelectedId()); };
    addAndMakeVisible (excitationCombo);

    styleAccentButton (noiseButton);
    noiseButton.setButtonText (strings::startNoiseButton);
    noiseButton.onClick = [this]
    {
        if (engine.isNoiseAudible())
        {
            eeq::log::write ("excitation button: stop clicked");
            engine.stopExcitation();
        }
        else
        {
            eeq::log::write ("excitation button: start clicked");
            engine.startExcitation();
        }
    };
    addAndMakeVisible (noiseButton);

    levelTitle.setText (strings::outputLevelLabel, juce::dontSendNotification);
    levelTitle.setJustificationType (juce::Justification::centredRight);
    styleLabel (levelTitle);
    addAndMakeVisible (levelTitle);

    styleSlider (levelSlider);
    levelSlider.setRange (-60.0, 0.0, 0.5);
    levelSlider.setValue (engine.getNoiseLevelDb(), juce::dontSendNotification);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
    levelSlider.setTextValueSuffix (" dBFS");
    levelSlider.addListener (this);
    addAndMakeVisible (levelSlider);

    statusLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    statusLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);
}

void BottomBar::applyExcitationSource (int comboId)
{
    if (comboId == lastExcitationId)
        return;

    lastExcitationId = comboId;
    auto& measurement = engine.getMeasurement();

    switch (comboId)
    {
        case 2:
        {
            // ログスイープ（既定 10 秒。長さは IR・遅延タブで変えられる。仕様 3.2: 2〜30秒）
            const auto sampleRate = engine.getCurrentSampleRate();
            auto sweep = eeq::dsp::LogSweepGenerator::generate (sampleRate, 10.0, 20.0, 20000.0,
                                                                (float) juce::Decibels::decibelsToGain (engine.getNoiseLevelDb()));
            engine.setSweep (std::move (sweep));
            break;
        }

        case 3:
        {
            // WAVファイルを選んで読み込む
            auto chooser = std::make_shared<juce::FileChooser> (strings::wavFileChooserTitle,
                                                                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                                "*.wav;*.aiff;*.aif");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser] (const juce::FileChooser& fc)
                {
                    const auto file = fc.getResult();

                    if (file == juce::File())
                    {
                        excitationCombo.setSelectedId (1, juce::sendNotificationSync);
                        return;
                    }

                    juce::String error;

                    if (! engine.loadWavExcitation (file, error))
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                            strings::wavLoadFailed, error, strings::okButton, nullptr, nullptr);
                        excitationCombo.setSelectedId (1, juce::sendNotificationSync);
                    }
                });
            break;
        }

        case 4:
            engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::external);
            break;

        case 1:
        default:
            engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
            break;
    }
}

void BottomBar::update()
{
    const auto audible = engine.isNoiseAudible();
    noiseButton.setButtonText (audible ? strings::stopNoiseButton : strings::startNoiseButton);
    noiseButton.setColour (juce::TextButton::buttonColourId,
                           audible ? juce::Colour (0xff8c3a3a) : juce::Colour (0xff2e5a63));

    const auto measuring = engine.getMeasurement().isMeasuring();
    measurementButton.setButtonText (measuring ? strings::measurementStopButton : strings::measurementStartButton);
    measurementButton.setColour (juce::TextButton::buttonColourId,
                                 measuring ? juce::Colour (0xff8c3a3a) : juce::Colour (0xff2e5a63));

    // 出力レベルは他タブ（リミットテスト等）からも変更されるため、ドラッグ中に
    // なければエンジンの現在値へ同期する
    if (! levelSlider.isMouseButtonDown()
        && std::abs (levelSlider.getValue() - (double) engine.getNoiseLevelDb()) > 0.01)
        levelSlider.setValue ((double) engine.getNoiseLevelDb(), juce::dontSendNotification);
}

void BottomBar::setStatusText (const juce::String& text)
{
    if (statusLabel.getText() != text)
        statusLabel.setText (text, juce::dontSendNotification);
}

void BottomBar::paint (juce::Graphics& g)
{
    g.setColour (theme::get (theme::ColourId::panelBackground));
    g.fillRect (getLocalBounds());
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.fillRect (getLocalBounds().removeFromTop (1));
}

void BottomBar::resized()
{
    auto bounds = getLocalBounds().reduced (8, 8);

    measurementButton.setBounds (bounds.removeFromLeft (86).withSizeKeepingCentre (86, 30));
    bounds.removeFromLeft (6);
    resetButton.setBounds (bounds.removeFromLeft (96).withSizeKeepingCentre (96, 30));
    bounds.removeFromLeft (10);

    snapshotButton.setBounds (bounds.removeFromLeft (104).withSizeKeepingCentre (104, 30));
    bounds.removeFromLeft (6);
    delayButton.setBounds (bounds.removeFromLeft (72).withSizeKeepingCentre (72, 30));
    bounds.removeFromLeft (6);
    calibrationButton.setBounds (bounds.removeFromLeft (56).withSizeKeepingCentre (56, 30));
    bounds.removeFromLeft (12);

    excitationCombo.setBounds (bounds.removeFromLeft (128).withSizeKeepingCentre (128, 26));
    bounds.removeFromLeft (6);
    noiseButton.setBounds (bounds.removeFromLeft (104).withSizeKeepingCentre (104, 30));
    bounds.removeFromLeft (12);

    levelTitle.setBounds (bounds.removeFromLeft (70));
    bounds.removeFromLeft (6);
    levelSlider.setBounds (bounds.removeFromLeft (juce::jmin (250, bounds.getWidth())).withSizeKeepingCentre (
        juce::jmin (250, bounds.getWidth()), 30));

    statusLabel.setBounds (bounds.withTrimmedLeft (12));
}

void BottomBar::sliderValueChanged (juce::Slider* slider)
{
    if (slider != &levelSlider)
        return;

    const auto value = (float) levelSlider.getValue();
    const auto limit = eeq::audio::AudioEngine::outputLevelLimitDb;

    if (value <= limit)
    {
        overLimitConfirmed = false;
        engine.setNoiseLevelDb (value);
        return;
    }

    // 上限超え：未承認ならエンジンへは適用せず、スライダー値だけ動かす
    // ドラッグ中は limit でクランプした値を出しておく（音が出ないように）
    if (! overLimitConfirmed)
    {
        if (levelSlider.isMouseButtonDown())
            engine.setNoiseLevelDb (limit);
        // テキスト入力等の即確認の場合は confirmOverLimitIfNeeded で処理
        else
            confirmOverLimitIfNeeded();
    }
    else
    {
        // 承認済みならそのまま適用
        engine.setNoiseLevelDb (value);
    }
}

void BottomBar::sliderDragEnded (juce::Slider* slider)
{
    if (slider == &levelSlider)
        confirmOverLimitIfNeeded();
}

void BottomBar::confirmOverLimitIfNeeded()
{
    const auto value = (float) levelSlider.getValue();
    const auto limit = eeq::audio::AudioEngine::outputLevelLimitDb;

    if (value <= limit || overLimitConfirmed || dialogOpen)
        return;

    dialogOpen = true;
    juce::Component::SafePointer<BottomBar> safeThis { this };

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
        strings::overLimitTitle, strings::overLimitMessage,
        strings::overLimitOk, strings::overLimitCancel, this,
        juce::ModalCallbackFunction::create ([safeThis, value, limit] (int result) mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->dialogOpen = false;

            if (result == 0)
            {
                // キャンセル：−20 dBFS に戻す
                safeThis->levelSlider.setValue ((double) limit, juce::sendNotificationSync);
            }
            else
            {
                // OK：承認フラグを立てて、エンジンへ適用
                safeThis->overLimitConfirmed = true;
                safeThis->engine.setNoiseLevelDb (value);
            }
        }));
}

} // namespace eeq::ui
