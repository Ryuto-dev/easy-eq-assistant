#include "MainComponent.h"

#include "Core/Log.h"
#include "UI/Strings.h"

MainComponent::MainComponent()
    : engine (appPropertiesStorage.properties.getUserSettings()),
      topBar (engine),
      bottomBar (engine)
{
    eeq::log::write ("--- Easy EQ assistant started (phase 2) ---");
    setOpaque (true);

    topBar.onError = [this] (const juce::String& message) { showError (message); };
    measurementPanel.onSettingsChanged = [this] { applyMeasurementSettings(); };

    addAndMakeVisible (topBar);
    addAndMakeVisible (bottomBar);
    addAndMakeVisible (graphView);
    addAndMakeVisible (measurementPanel);

    setSize (1200, 780);
    applyMeasurementSettings();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.saveSettings();
    appPropertiesStorage.properties.getUserSettings()->saveIfNeeded();
}

void MainComponent::paint (juce::Graphics& g)
{
    // ダークテーマ（仕様 5.）。グラフの下地は FrequencyGraphView が描く
    g.fillAll (juce::Colour (0xff141518));

    auto canvas = getLocalBounds().reduced (10)
                      .withTrimmedTop (topBarHeight)
                      .withTrimmedBottom (bottomBarHeight);

    g.setColour (juce::Colour (0xff1e2025));
    g.fillRoundedRectangle (canvas.toFloat(), 8.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    topBar.setBounds (area.removeFromTop (topBarHeight));
    bottomBar.setBounds (area.removeFromBottom (bottomBarHeight));

    auto canvas = area.reduced (14);
    measurementPanel.setBounds (canvas.removeFromRight (225));
    graphView.setBounds (canvas);
}

void MainComponent::timerCallback()
{
    topBar.timerTick();
    bottomBar.update();

    // 解析結果を取得してグラフへ（約30fps: 仕様 2. の (3)）
    engine.getMeasurement().copyLatestResult (latestFrame);
    graphView.setResult (latestFrame, measurementPanel.getSmoothingOctaveDenominator());
    measurementPanel.setAverageCountDisplay (engine.getMeasurement().getAverages());

    const auto status = engine.getStatusText();
    bottomBar.setStatusText (status.isNotEmpty() ? status : eeq::strings::noDeviceInfo);

    // デバイスが開き直されたら測定設定（サンプルレート）を合わせ直す
    if (engine.getDeviceGeneration() != lastSeenGeneration)
    {
        lastSeenGeneration = engine.getDeviceGeneration();
        applyMeasurementSettings();
    }

    if (engine.consumeNoiseReachedIdle())
        eeq::log::write ("noise: reached idle (fade-out completed on audio thread)");

    juce::String deviceError;

    if (engine.consumeDeviceError (deviceError))
    {
        showError (deviceError.isNotEmpty()
                       ? eeq::strings::deviceErrorMessage + "\n\n" + deviceError
                       : eeq::strings::deviceErrorMessage);
    }
}

void MainComponent::applyMeasurementSettings()
{
    engine.getMeasurement().configure (measurementPanel.getFftSize(), engine.getCurrentSampleRate(),
                                       measurementPanel.getAveragingConfig(), measurementPanel.getWindowMethod());
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
