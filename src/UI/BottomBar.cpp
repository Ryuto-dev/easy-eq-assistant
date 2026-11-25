#include "BottomBar.h"

#include "Strings.h"

namespace eeq::ui
{

BottomBar::BottomBar (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn)
{
    noiseButton.setButtonText (strings::startNoiseButton);
    noiseButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e5a63));
    noiseButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe4e6ea));
    noiseButton.onClick = [this]
    {
        if (engine.isNoiseAudible())
            engine.stopNoise();
        else
            engine.startNoise();
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
}

void BottomBar::update()
{
    const auto audible = engine.isNoiseAudible();
    noiseButton.setButtonText (audible ? strings::stopNoiseButton : strings::startNoiseButton);
    noiseButton.setColour (juce::TextButton::buttonColourId,
                           audible ? juce::Colour (0xff8c3a3a) : juce::Colour (0xff2e5a63));
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

    noiseButton.setBounds (bounds.removeFromLeft (150).withSizeKeepingCentre (150, 30));
    bounds.removeFromLeft (24);

    levelTitle.setBounds (bounds.removeFromLeft (80));
    bounds.removeFromLeft (8);
    levelSlider.setBounds (bounds.removeFromLeft (juce::jmin (360, bounds.getWidth())).withSizeKeepingCentre (juce::jmin (360, bounds.getWidth()), 30));
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
