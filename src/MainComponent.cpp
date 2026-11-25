#include "MainComponent.h"

#include "UI/Strings.h"

MainComponent::MainComponent()
{
    setOpaque (true);
    setSize (1100, 700);

    statusLabel.setText (eeq::strings::phaseMessage, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);
}

MainComponent::~MainComponent() = default;

void MainComponent::paint (juce::Graphics& g)
{
    // ダークテーマ（仕様 5.）。フェーズ2 以降で本格的なグラフ表示に置き換える
    g.fillAll (juce::Colour (0xff1b1d21));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (32.0f, juce::Font::bold)));
    g.drawText (eeq::strings::appName, getLocalBounds(), juce::Justification::centred);
}

void MainComponent::resized()
{
    statusLabel.setBounds (getLocalBounds().removeFromBottom (80).reduced (16, 0));
}
