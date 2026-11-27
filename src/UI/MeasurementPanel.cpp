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

    for (int i = 0; i < (int) std::size (fftSizes); ++i)
        fftCombo.addItem (juce::String (fftSizes[i]), i + 1);

    fftCombo.setSelectedId (Defaults::fftId, juce::dontSendNotification);

    windowLabel.setText (strings::windowLabel, juce::dontSendNotification);
    windowCombo.addItem ("Hann", 1);
    windowCombo.addItem ("Blackman-Harris", 2);
    windowCombo.addItem ("Flat-top", 3);
    windowCombo.setSelectedId (Defaults::windowId, juce::dontSendNotification);

    avgModeLabel.setText (strings::averagingModeLabel, juce::dontSendNotification);
    avgModeCombo.addItem (strings::averagingModeCumulative, 1);
    avgModeCombo.addItem (strings::averagingModeExponential, 2);
    avgModeCombo.addItem (strings::averagingModeFifo, 3);
    avgModeCombo.setSelectedId (Defaults::avgModeId, juce::dontSendNotification);

    smoothingLabel.setText (strings::smoothingLabel, juce::dontSendNotification);
    smoothingCombo.addItem (strings::noSmoothingItem, 1);

    for (int i = 1; i < (int) std::size (smoothingDenominators); ++i)
        smoothingCombo.addItem ("1/" + juce::String (smoothingDenominators[i]) + " oct", i + 1);

    smoothingCombo.setSelectedId (Defaults::smoothingId, juce::dontSendNotification);

    avgCountLabel.setText (strings::averagingCountLabel, juce::dontSendNotification);
    updateCountItems();

    for (int i = 0; i < (int) std::size (averageCounts); ++i)
        if (averageCounts[i] == Defaults::avgCount)
            avgCountCombo.setSelectedId (i + 1, juce::dontSendNotification);

    averagesDisplay.setColour (juce::Label::textColourId, theme::get (theme::ColourId::textDim));
    averagesDisplay.setFont (juce::Font (juce::FontOptions (11.5f)));
    averagesDisplay.setText (strings::averagesPrefix + "0" + strings::averagesSuffix, juce::dontSendNotification);

    for (auto* combo : { &fftCombo, &windowCombo, &avgModeCombo, &smoothingCombo, &avgCountCombo })
    {
        styleCombo (*combo);
        combo->addListener (this);
        addAndMakeVisible (combo);
    }

    addAndMakeVisible (averagesDisplay);
}

void MeasurementPanel::updateCountItems()
{
    // FIFO はフレームを保持するので上限を設ける（メモリ: bins × 16 byte × N）
    const auto selectedId = avgCountCombo.getSelectedId();
    const bool fifo = avgModeCombo.getSelectedId() == 3;
    avgCountCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) std::size (averageCounts); ++i)
    {
        avgCountCombo.addItem (juce::String (averageCounts[i]), i + 1);

        if (fifo && averageCounts[i] > eeq::dsp::AveragingConfig::maxFifoFrames)
            avgCountCombo.setItemEnabled (i + 1, false);
    }

    auto id = selectedId > 0 ? selectedId : 5;

    while (fifo && id > 1 && averageCounts[id - 1] > eeq::dsp::AveragingConfig::maxFifoFrames)
        --id;

    avgCountCombo.setSelectedId (id, juce::dontSendNotification);
}

void MeasurementPanel::setSettings (juce::PropertiesFile* settingsToUse)
{
    settings = settingsToUse;

    if (settings == nullptr)
        return;

    // 前回の測定設定を復元する（既定値は Defaults の1か所: issue #134）
    const juce::ScopedValueSetter<bool> guard (loadingSettings, true);

    fftCombo.setSelectedId (juce::jlimit (1, 4, settings->getIntValue ("measurementFftId", Defaults::fftId)),
                            juce::dontSendNotification);
    windowCombo.setSelectedId (juce::jlimit (1, 3, settings->getIntValue ("measurementWindowId", Defaults::windowId)),
                               juce::dontSendNotification);
    avgModeCombo.setSelectedId (juce::jlimit (1, 3, settings->getIntValue ("measurementAvgModeId", Defaults::avgModeId)),
                                juce::dontSendNotification);
    smoothingCombo.setSelectedId (juce::jlimit (1, (int) std::size (smoothingDenominators),
                                                settings->getIntValue ("measurementSmoothingId", Defaults::smoothingId)),
                                  juce::dontSendNotification);

    // 平均回数は保存値に最も近いプリセットへ寄せる（旧バージョンの連続値にも対応）
    const auto savedCount = settings->getDoubleValue ("measurementAvgCount", (double) Defaults::avgCount);
    int bestId = 1;

    for (int i = 0; i < (int) std::size (averageCounts); ++i)
        if (std::abs (std::log2 ((double) averageCounts[i]) - std::log2 (juce::jmax (1.0, savedCount)))
            < std::abs (std::log2 ((double) averageCounts[bestId - 1]) - std::log2 (juce::jmax (1.0, savedCount))))
            bestId = i + 1;

    avgCountCombo.setSelectedId (bestId, juce::dontSendNotification);
    updateCountItems();
}

void MeasurementPanel::saveSettings()
{
    if (settings == nullptr || loadingSettings)
        return;

    settings->setValue ("measurementFftId", fftCombo.getSelectedId());
    settings->setValue ("measurementWindowId", windowCombo.getSelectedId());
    settings->setValue ("measurementAvgModeId", avgModeCombo.getSelectedId());
    settings->setValue ("measurementSmoothingId", smoothingCombo.getSelectedId());
    settings->setValue ("measurementAvgCount", getAveragingConfig().maxAverages);
    settings->saveIfNeeded();
}

int MeasurementPanel::getFftSize() const noexcept
{
    const auto id = fftCombo.getSelectedId();
    return (id >= 1 && id <= (int) std::size (fftSizes)) ? fftSizes[id - 1] : fftSizes[Defaults::fftId - 1];
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
    eeq::dsp::AveragingConfig config;

    switch (avgModeCombo.getSelectedId())
    {
        case 1:  config.mode = eeq::dsp::AveragingConfig::Mode::cumulative; break;
        case 2:  config.mode = eeq::dsp::AveragingConfig::Mode::exponential; break;
        default: config.mode = eeq::dsp::AveragingConfig::Mode::fifo; break;
    }

    const auto id = avgCountCombo.getSelectedId();
    config.maxAverages = (id >= 1 && id <= (int) std::size (averageCounts)) ? averageCounts[id - 1] : Defaults::avgCount;

    if (config.mode == eeq::dsp::AveragingConfig::Mode::fifo)
        config.maxAverages = juce::jmin (config.maxAverages, eeq::dsp::AveragingConfig::maxFifoFrames);

    return config;
}

int MeasurementPanel::getSmoothingOctaveDenominator() const noexcept
{
    const auto id = smoothingCombo.getSelectedId();
    return (id >= 1 && id <= (int) std::size (smoothingDenominators)) ? smoothingDenominators[id - 1] : 6;
}

void MeasurementPanel::setAverageCountDisplay (int averages)
{
    const auto config = getAveragingConfig();
    auto newText = strings::averagesPrefix + juce::String (averages);

    if (config.mode != eeq::dsp::AveragingConfig::Mode::cumulative)
        newText << " / " << config.maxAverages;

    newText << strings::averagesSuffix;

    if (averagesDisplay.getText() != newText)
        averagesDisplay.setText (newText, juce::dontSendNotification);
}

void MeasurementPanel::comboBoxChanged (juce::ComboBox* combo)
{
    if (combo == &avgModeCombo)
        updateCountItems();

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
    row (avgCountLabel, avgCountCombo);
    row (smoothingLabel, smoothingCombo);

    averagesDisplay.setBounds (bounds.removeFromTop (16));
}

} // namespace eeq::ui
