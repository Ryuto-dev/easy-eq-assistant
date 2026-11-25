#include "MainComponent.h"

#include "UI/Strings.h"

MainComponent::MainComponent()
    : engine (appProperties.getUserSettings()),
      topBar (engine),
      bottomBar (engine)
{
    setOpaque (true);

    topBar.onError = [this] (const juce::String& message) { showError (message); };

    addAndMakeVisible (topBar);
    addAndMakeVisible (bottomBar);

    canvasTitle.setText (eeq::strings::appName, juce::dontSendNotification);
    canvasTitle.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
    canvasTitle.setJustificationType (juce::Justification::centred);
    canvasTitle.setColour (juce::Label::textColourId, juce::Colour (0xffc9cdd4));
    addAndMakeVisible (canvasTitle);

    canvasNote.setText (eeq::strings::phaseMessage, juce::dontSendNotification);
    canvasNote.setFont (juce::Font (juce::FontOptions (14.0f)));
    canvasNote.setJustificationType (juce::Justification::centred);
    canvasNote.setColour (juce::Label::textColourId, juce::Colour (0xff8a9099));
    addAndMakeVisible (canvasNote);

    setSize (1200, 780);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.saveSettings();
    appProperties.getUserSettings()->saveIfNeeded();
}

void MainComponent::paint (juce::Graphics& g)
{
    // ダークテーマ（仕様 5.）。フェーズ2 でこの領域にグラフを描く
    g.fillAll (juce::Colour (0xff141518));

    auto canvas = getLocalBounds().reduced (12)
                      .withTrimmedTop (topBarHeight)
                      .withTrimmedBottom (bottomBarHeight);

    g.setColour (juce::Colour (0xff1e2025));
    g.fillRoundedRectangle (canvas.toFloat(), 8.0f);
    g.setColour (juce::Colour (0xff2c2f36));
    g.drawRoundedRectangle (canvas.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    topBar.setBounds (area.removeFromTop (topBarHeight));
    bottomBar.setBounds (area.removeFromBottom (bottomBarHeight));

    auto canvas = area.reduced (12);
    canvasTitle.setBounds (canvas.removeFromTop (56));
    canvasNote.setBounds (canvas.withSizeKeepingCentre (juce::jmin (780, canvas.getWidth()), 24));
}

void MainComponent::timerCallback()
{
    topBar.timerTick();
    bottomBar.update();

    const auto status = engine.getStatusText();
    canvasNote.setText (status.isNotEmpty() ? status : eeq::strings::noDeviceInfo, juce::dontSendNotification);

    juce::String deviceError;

    if (engine.consumeDeviceError (deviceError))
    {
        showError (deviceError.isNotEmpty()
                       ? eeq::strings::deviceErrorMessage + "\n\n" + deviceError
                       : eeq::strings::deviceErrorMessage);
    }
}

void MainComponent::showError (const juce::String& message)
{
    if (errorDialogOpen)
        return;

    errorDialogOpen = true;
    juce::Component::SafePointer<MainComponent> safeThis { this };

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
        eeq::strings::deviceErrorTitle, message, eeq::strings::okButton, this,
        juce::ModalCallbackFunction::create ([safeThis] (int)
        {
            if (safeThis != nullptr)
                safeThis->errorDialogOpen = false;
        }));
}
