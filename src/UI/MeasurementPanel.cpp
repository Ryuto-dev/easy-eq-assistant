#include "MeasurementPanel.h"

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

MeasurementPanel::MeasurementPanel()
{
    titleLabel.setText (strings::measurementPanelTitle, juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
    addAndMakeVisible (titleLabel);

    for (auto* label : { &fftLabel, &windowLabel, &avgModeLabel, &avgCountLabel, &smoothingLabel })
    {
        styleLabel (*label);
        addAndMakeVisible (label);
    }

    fftLabel.setText (strings::fftSizeLabel, juce::dontSendNotification);
    fftCombo.addItem ("8192", 1);
    fftCombo.addItem ("16384", 2);
    fftCombo.addItem ("32768", 3);
    fftCombo.addItem ("65536", 4);
    fftCombo.setSelectedId (1, juce::dontSendNotification); // 既定 8192（応答重視）

    windowLabel.setText (strings::windowLabel, juce::dontSendNotification);
    windowCombo.addItem ("Hann", 1);
    windowCombo.addItem ("Blackman-Harris", 2);
    windowCombo.addItem ("Flat-top", 3);
    windowCombo.setSelectedId (1, juce::dontSendNotification); // 既定 Hann

    avgModeLabel.setText (strings::averagingModeLabel, juce::dontSendNotification);
    avgModeCombo.addItem (strings::averagingModeCumulative, 1);
    avgModeCombo.addItem (strings::averagingModeExponential, 2);
    avgModeCombo.setSelectedId (2, juce::dontSendNotification); // 既定 指数平均（応答重視）

    smoothingLabel.setText (strings::smoothingLabel, juce::dontSendNotification);
    smoothingCombo.addItem (strings::noSmoothingItem, 1);       // 0: なし
    smoothingCombo.addItem ("1/48 oct", 2);                      // 48
    smoothingCombo.addItem ("1/24 oct", 3);                      // 24
    smoothingCombo.addItem ("1/12 oct", 4);                      // 12
    smoothingCombo.addItem ("1/6 oct", 5);                       // 6 (既定)
    smoothingCombo.addItem ("1/3 oct", 6);                       // 3
    smoothingCombo.addItem ("1/2 oct", 7);                       // 2
    smoothingCombo.addItem ("1/1 oct", 8);                       // 1
    smoothingCombo.addItem (strings::smoothingFifoLabel, 9);     // -1: FIFO
    smoothingCombo.setSelectedId (5, juce::dontSendNotification); // 既定 1/6 oct

    avgCountLabel.setText (strings::averagingCountLabel, juce::dontSendNotification);
    avgCountSlider.setRange (1.0, 256.0, 1.0);
    avgCountSlider.setValue (16.0, juce::dontSendNotification); // 既定 16（応答重視）
    avgCountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    avgCountSlider.setColour (juce::Slider::backgroundColourId, theme::get (theme::ColourId::control));
    avgCountSlider.setColour (juce::Slider::trackColourId, theme::get (theme::ColourId::accent));
    avgCountSlider.setColour (juce::Slider::thumbColourId, theme::get (theme::ColourId::text));
    avgCountSlider.setColour (juce::Slider::textBoxTextColourId, theme::get (theme::ColourId::text));
    avgCountSlider.setColour (juce::Slider::textBoxBackgroundColourId, theme::get (theme::ColourId::control));
    avgCountSlider.setColour (juce::Slider::textBoxOutlineColourId, theme::get (theme::ColourId::controlOutline));

    averagesDisplay.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    averagesDisplay.setFont (juce::Font (juce::FontOptions (11.5f)));
    averagesDisplay.setText (juce::String (u8"平均: 0 回"), juce::dontSendNotification);

    // FIFO 時定数入力（issue #147）
    fifoTimeLabel.setText (strings::smoothingFifoTimeLabel, juce::dontSendNotification);
    styleLabel (fifoTimeLabel);
    addAndMakeVisible (fifoTimeLabel);

    styleTextEditor (fifoTimeEditor);
    fifoTimeEditor.setText ("0.5", juce::dontSendNotification);
    fifoTimeEditor.setInputRestrictions (6, "0123456789.");
    fifoTimeEditor.onTextChange = [this]
    {
        saveSettings();
        if (onSettingsChanged)
            onSettingsChanged();
    };
    addAndMakeVisible (fifoTimeEditor);

    for (auto* combo : { &fftCombo, &windowCombo, &avgModeCombo, &smoothingCombo })
    {
        styleCombo (*combo);
        combo->addListener (this);
        addAndMakeVisible (combo);
    }

    avgCountSlider.addListener (this);
    addAndMakeVisible (avgCountSlider);
    addAndMakeVisible (averagesDisplay);

    updateSmoothingControls();
}

void MeasurementPanel::updateSmoothingControls()
{
    const bool isFifo = (smoothingCombo.getSelectedId() == 9); // FIFO 選択時
    fifoTimeLabel.setVisible (isFifo);
    fifoTimeEditor.setVisible (isFifo);
    useFifo = isFifo;
}

void MeasurementPanel::setSettings (juce::PropertiesFile* settingsToUse)
{
    settings = settingsToUse;

    if (settings == nullptr)
        return;

    // 前回の測定設定を復元する
    const juce::ScopedValueSetter<bool> guard (loadingSettings, true);

    fftCombo.setSelectedId (juce::jlimit (1, 4, settings->getIntValue ("measurementFftId", 3)),
                            juce::dontSendNotification);
    windowCombo.setSelectedId (juce::jlimit (1, 3, settings->getIntValue ("measurementWindowId", 1)),
                               juce::dontSendNotification);
    avgModeCombo.setSelectedId (juce::jlimit (1, 2, settings->getIntValue ("measurementAvgModeId", 1)),
                                juce::dontSendNotification);
    smoothingCombo.setSelectedId (juce::jlimit (1, 9, settings->getIntValue ("measurementSmoothingId", 5)),
                                  juce::dontSendNotification);
    avgCountSlider.setValue (settings->getDoubleValue ("measurementAvgCount", 16.0), juce::dontSendNotification);
    fifoTimeEditor.setText (settings->getValue ("measurementFifoTime", "0.5"), juce::dontSendNotification);

    updateSmoothingControls();
}

void MeasurementPanel::saveSettings()
{
    if (settings == nullptr || loadingSettings)
        return;

    settings->setValue ("measurementFftId", fftCombo.getSelectedId());
    settings->setValue ("measurementWindowId", windowCombo.getSelectedId());
    settings->setValue ("measurementAvgModeId", avgModeCombo.getSelectedId());
    settings->setValue ("measurementSmoothingId", smoothingCombo.getSelectedId());
    settings->setValue ("measurementAvgCount", avgCountSlider.getValue());
    settings->setValue ("measurementFifoTime", fifoTimeEditor.getText());
    settings->saveIfNeeded();
}

int MeasurementPanel::getFftSize() const noexcept
{
    const auto id = fftCombo.getSelectedId();
    static const int fftSizes[] = { 8192, 16384, 32768, 65536 };
    return (id >= 1 && id <= 4) ? fftSizes[id - 1] : 8192;
}

juce::dsp::WindowingFunction<float>::WindowingMethod MeasurementPanel::getWindowMethod() const noexcept
{
    switch (windowCombo.getSelectedId())
    {
        case 2: return juce::dsp::WindowingFunction<float>::blackmanHarris;
        case 3: return juce::dsp::WindowingFunction<float>::flatTop;
        default: return juce::dsp::WindowingFunction<float>::hann;
    }
}

eeq::dsp::AveragingConfig MeasurementPanel::getAveragingConfig() const noexcept
{
    return { avgModeCombo.getSelectedId() == 2 ? eeq::dsp::AveragingConfig::Mode::exponential
                                               : eeq::dsp::AveragingConfig::Mode::cumulative,
             juce::jlimit (1, 256, (int) avgCountSlider.getValue()) };
}

int MeasurementPanel::getSmoothingOctaveDenominator() const noexcept
{
    const auto id = smoothingCombo.getSelectedId();
    if (id == 9) return -1; // FIFO
    return id >= 2 && id <= 9 ? smoothingDenominators[id - 2] : 0; // id 1 = なし
}

double MeasurementPanel::getFifoTimeConstant() const noexcept
{
    if (! useFifo) return 0.0;
    return std::max (0.01, fifoTimeEditor.getText().getDoubleValue());
}

void MeasurementPanel::setAverageCountDisplay (int averages)
{
    const auto newText = juce::String (u8"平均: ") + juce::String (averages) + juce::String (u8" 回");

    if (averagesDisplay.getText() != newText)
        averagesDisplay.setText (newText, juce::dontSendNotification);
}

void MeasurementPanel::comboBoxChanged (juce::ComboBox* combo)
{
    if (combo == &smoothingCombo)
        updateSmoothingControls();

    saveSettings();

    if (onSettingsChanged)
        onSettingsChanged();
}

void MeasurementPanel::sliderValueChanged (juce::Slider*)
{
    saveSettings();

    if (onSettingsChanged)
        onSettingsChanged();
}

void MeasurementPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::get (theme::ColourId::panelBackground));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void MeasurementPanel::resized()
{
    auto bounds = getLocalBounds().reduced (12, 8);

    titleLabel.setBounds (bounds.removeFromTop (20));
    bounds.removeFromTop (8);

    auto row = [&bounds] (juce::Label& label, juce::Component& control)
    {
        label.setBounds (bounds.removeFromTop (14));
        bounds.removeFromTop (2);
        control.setBounds (bounds.removeFromTop (24));
        bounds.removeFromTop (8);
    };

    row (fftLabel, fftCombo);
    row (windowLabel, windowCombo);
    row (avgModeLabel, avgModeCombo);
    row (smoothingLabel, smoothingCombo);

    // FIFO 時定数行（表示時のみスペースを取る）
    if (useFifo)
    {
        auto fifoRow = bounds.removeFromTop (40);
        fifoTimeLabel.setBounds (fifoRow.removeFromTop (14));
        fifoRow.removeFromTop (2);
        fifoTimeEditor.setBounds (fifoRow.removeFromTop (24));
        bounds.removeFromTop (8);
    }

    row (avgCountLabel, avgCountSlider);

    averagesDisplay.setBounds (bounds.removeFromTop (16));
}

} // namespace eeq::ui
