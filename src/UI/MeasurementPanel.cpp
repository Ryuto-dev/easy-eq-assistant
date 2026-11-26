#include "MeasurementPanel.h"

#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{
namespace
{
void styleCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff26282e));
    combo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff34373f));
    combo.setColour (juce::ComboBox::textColourId, juce::Colour (0xffe4e6ea));
    combo.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff8a9099));
}

void styleLabel (juce::Label& label)
{
    label.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a8));
    label.setFont (juce::Font (juce::FontOptions (12.0f)));
}
} // namespace

MeasurementPanel::MeasurementPanel()
{
    titleLabel.setText (strings::measurementPanelTitle, juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc9cdd4));
    addAndMakeVisible (titleLabel);

    for (auto* label : { &fftLabel, &windowLabel, &avgModeLabel, &avgCountLabel, &smoothingLabel })
    {
        styleLabel (*label);
        addAndMakeVisible (label);
    }

    fftLabel.setText (strings::fftSizeLabel, juce::dontSendNotification);
    fftCombo.addItem ("8192", 1);
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
    smoothingCombo.addItem (strings::noSmoothingItem, 1);
    smoothingCombo.addItem ("1/48", 2);
    smoothingCombo.addItem ("1/24", 3);
    smoothingCombo.addItem ("1/12", 4);
    smoothingCombo.addItem ("1/6", 5);
    smoothingCombo.addItem ("1/3", 6);
    smoothingCombo.setSelectedId (5, juce::dontSendNotification); // 既定 1/6 oct

    avgCountLabel.setText (strings::averagingCountLabel, juce::dontSendNotification);
    avgCountSlider.setRange (1.0, 256.0, 1.0);
    avgCountSlider.setValue (16.0, juce::dontSendNotification); // 既定 16（応答重視）
    avgCountSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    avgCountSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff26282e));
    avgCountSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3fc9d2));
    avgCountSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffdfe2e6));
    avgCountSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffe4e6ea));
    avgCountSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff26282e));
    avgCountSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff34373f));

    averagesDisplay.setColour (juce::Label::textColourId, juce::Colour (0xff777d86));
    averagesDisplay.setFont (juce::Font (juce::FontOptions (11.5f)));
    averagesDisplay.setText (juce::String (u8"平均: 0 回"), juce::dontSendNotification);

    for (auto* combo : { &fftCombo, &windowCombo, &avgModeCombo, &smoothingCombo })
    {
        styleCombo (*combo);
        combo->addListener (this);
        addAndMakeVisible (combo);
    }

    avgCountSlider.addListener (this);
    addAndMakeVisible (avgCountSlider);
    addAndMakeVisible (averagesDisplay);
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
    smoothingCombo.setSelectedId (juce::jlimit (1, 6, settings->getIntValue ("measurementSmoothingId", 5)),
                                  juce::dontSendNotification);
    avgCountSlider.setValue (settings->getDoubleValue ("measurementAvgCount", 64.0), juce::dontSendNotification);
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
    return id >= 2 && id <= 6 ? smoothingDenominators[id - 2] : 0; // id 1 = なし
}

void MeasurementPanel::setAverageCountDisplay (int averages)
{
    const auto newText = juce::String (u8"平均: ") + juce::String (averages) + juce::String (u8" 回");

    if (averagesDisplay.getText() != newText)
        averagesDisplay.setText (newText, juce::dontSendNotification);
}

void MeasurementPanel::comboBoxChanged (juce::ComboBox*)
{
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
    row (avgCountLabel, avgCountSlider);

    averagesDisplay.setBounds (bounds.removeFromTop (16));
}

} // namespace eeq::ui
