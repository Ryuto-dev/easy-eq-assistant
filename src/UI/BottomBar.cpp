#include "BottomBar.h"

#include "../Core/Log.h"
#include "Strings.h"

namespace eeq::ui
{

BottomBar::BottomBar (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn)
{
    measurementButton.setButtonText (strings::measurementStartButton);
    measurementButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e5a63));
    measurementButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe4e6ea));
    measurementButton.setWantsKeyboardFocus (false);
    measurementButton.setMouseClickGrabsKeyboardFocus (false);
    measurementButton.onClick = [this]
    {
        auto& measurement = engine.getMeasurement();

        if (measurement.isMeasuring())
            measurement.stopMeasurement();
        else
            measurement.startMeasurement();
    };
    addAndMakeVisible (measurementButton);

    resetButton.setButtonText (strings::resetAveragesButton);
    resetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff33363d));
    resetButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe4e6ea));
    resetButton.setWantsKeyboardFocus (false);
    resetButton.setMouseClickGrabsKeyboardFocus (false);
    resetButton.onClick = [this]
    {
        engine.getMeasurement().resetAverages();
        engine.clearClipLatches();
    };
    addAndMakeVisible (resetButton);

    noiseButton.setButtonText (strings::startNoiseButton);
    noiseButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e5a63));
    noiseButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe4e6ea));
    // マウス専用にする（クリックでフォーカスを取らず、Space/Enterの誤発火を防ぐ）
    noiseButton.setWantsKeyboardFocus (false);
    noiseButton.setMouseClickGrabsKeyboardFocus (false);
    noiseButton.onClick = [this]
    {
        if (engine.isNoiseAudible())
        {
            eeq::log::write ("noise button: stop clicked");
            engine.stopNoise();
        }
        else
        {
            eeq::log::write ("noise button: start clicked");
            engine.startNoise();
        }
    };
    addAndMakeVisible (noiseButton);

    levelTitle.setText (strings::outputLevelLabel, juce::dontSendNotification);
    levelTitle.setJustificationType (juce::Justification::centredRight);
    levelTitle.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a8));
    levelTitle.setFont (juce::Font (juce::FontOptions (12.5f)));
    addAndMakeVisible (levelTitle);

    levelSlider.setRange (-60.0, 0.0, 0.5);
    levelSlider.setValue (engine.getNoiseLevelDb(), juce::dontSendNotification);
    levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
    levelSlider.setTextValueSuffix (" dBFS");
    levelSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff26282e));
    levelSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3fc9d2));
    levelSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffdfe2e6));
    levelSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe4e6ea));
    levelSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff26282e));
    levelSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff34373f));
    levelSlider.addListener (this);
    addAndMakeVisible (levelSlider);

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff777d86));
    statusLabel.setFont (juce::Font (juce::FontOptions (11.5f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);
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
}

void BottomBar::setStatusText (const juce::String& text)
{
    if (statusLabel.getText() != text)
        statusLabel.setText (text, juce::dontSendNotification);
}

void BottomBar::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff1a1c20));
    g.fillRect (getLocalBounds());
    g.setColour (juce::Colour (0xff2c2f36));
    g.fillRect (getLocalBounds().removeFromTop (1));
}

void BottomBar::resized()
{
    auto bounds = getLocalBounds().reduced (12, 8);

    measurementButton.setBounds (bounds.removeFromLeft (96).withSizeKeepingCentre (96, 30));
    bounds.removeFromLeft (8);
    resetButton.setBounds (bounds.removeFromLeft (108).withSizeKeepingCentre (108, 30));
    bounds.removeFromLeft (20);

    noiseButton.setBounds (bounds.removeFromLeft (140).withSizeKeepingCentre (140, 30));
    bounds.removeFromLeft (24);

    levelTitle.setBounds (bounds.removeFromLeft (80));
    bounds.removeFromLeft (8);
    levelSlider.setBounds (bounds.removeFromLeft (juce::jmin (300, bounds.getWidth())).withSizeKeepingCentre (juce::jmin (300, bounds.getWidth()), 30));

    statusLabel.setBounds (bounds.withTrimmedLeft (12));
}

void BottomBar::sliderValueChanged (juce::Slider* slider)
{
    if (slider != &levelSlider)
        return;

    const auto value = (float) levelSlider.getValue();
    engine.setNoiseLevelDb (value);

    if (value <= eeq::audio::AudioEngine::outputLevelLimitDb)
    {
        overLimitConfirmed = false;
        return;
    }

    // ドラッグ中は確定時にまとめて確認する（テキストボックス入力は即確認）
    if (! levelSlider.isMouseButtonDown())
        confirmOverLimitIfNeeded();
}

void BottomBar::sliderDragEnded (juce::Slider* slider)
{
    if (slider == &levelSlider)
        confirmOverLimitIfNeeded();
}

void BottomBar::confirmOverLimitIfNeeded()
{
    if ((float) levelSlider.getValue() <= eeq::audio::AudioEngine::outputLevelLimitDb
        || overLimitConfirmed || dialogOpen)
        return;

    dialogOpen = true;
    juce::Component::SafePointer<BottomBar> safeThis { this };

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon,
        strings::overLimitTitle, strings::overLimitMessage,
        strings::overLimitOk, strings::overLimitCancel, this,
        juce::ModalCallbackFunction::create ([safeThis] (int result) mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->dialogOpen = false;

            if (result == 0)
                safeThis->levelSlider.setValue ((double) eeq::audio::AudioEngine::outputLevelLimitDb,
                                                juce::sendNotificationSync);
            else
                safeThis->overLimitConfirmed = true;
        }));
}

} // namespace eeq::ui
