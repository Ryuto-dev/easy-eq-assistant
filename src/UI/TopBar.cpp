#include "TopBar.h"

#include "Strings.h"

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
    label.setFont (juce::Font (juce::FontOptions (12.5f)));
}
} // namespace

TopBar::TopBar (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn),
      refMeter (strings::refLabel),
      measMeter (strings::measLabel),
      outMeter (strings::outLabel)
{
    deviceTitle.setText (strings::deviceLabel, juce::dontSendNotification);
    rateTitle.setText (strings::sampleRateLabel, juce::dontSendNotification);
    bufferTitle.setText (strings::bufferSizeLabel, juce::dontSendNotification);
    refTitle.setText (strings::refLabel, juce::dontSendNotification);
    measTitle.setText (strings::measLabel, juce::dontSendNotification);
    outTitle.setText (strings::outLabel, juce::dontSendNotification);

    for (auto* label : { &deviceTitle, &rateTitle, &bufferTitle, &refTitle, &measTitle, &outTitle })
    {
        styleLabel (*label);
        addAndMakeVisible (label);
    }

    for (auto* combo : { &deviceCombo, &rateCombo, &bufferCombo, &refCombo, &measCombo, &outCombo })
    {
        styleCombo (*combo);
        combo->addListener (this);
        addAndMakeVisible (combo);
    }

    for (auto* meter : { &refMeter, &measMeter, &outMeter })
        addAndMakeVisible (meter);

    rebuildAll();
}

void TopBar::timerTick()
{
    auto toDb = [] (float linear)
    { return juce::Decibels::gainToDecibels (linear, -100.0f); };

    auto applyMeter = [&toDb] (const eeq::audio::MeterState& source, LevelMeter& target)
    {
        target.setLevels (toDb (source.peakLinear.load (std::memory_order_relaxed)),
                          toDb (source.rmsLinear.load (std::memory_order_relaxed)),
                          source.clip.load (std::memory_order_relaxed));
    };

    applyMeter (engine.getRefMeter(), refMeter);
    applyMeter (engine.getMeasMeter(), measMeter);
    applyMeter (engine.getOutMeter(), outMeter);

    // デバイスが開き直されたらコンボを更新する
    if (engine.getDeviceGeneration() != lastGeneration)
        rebuildAll();
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff1a1c20));
    g.fillRect (getLocalBounds());
    g.setColour (juce::Colour (0xff2c2f36));
    g.fillRect (getLocalBounds().removeFromBottom (1));
}

void TopBar::resized()
{
    auto bounds = getLocalBounds().reduced (12, 5);

    auto row1 = bounds.removeFromTop (24);
    bounds.removeFromTop (6);
    auto row2 = bounds.removeFromTop (30);

    auto place = [] (juce::Component& component, juce::Rectangle<int>& area, int width)
    {
        component.setBounds (area.removeFromLeft (width));
        area.removeFromLeft (8);
    };

    // 1行目: [デバイス] [デバイス名（可変幅）] [サンプルレート] [▽] [バッファ] [▽]
    const auto fixedRightSide = 88 + 8 + 105 + 8 + 56 + 8 + 105; // レート+バッファ部分の合計幅
    place (deviceTitle, row1, 62);

    const auto deviceWidth = juce::jmax (140, row1.getWidth() - fixedRightSide - 8);
    deviceCombo.setBounds (row1.removeFromLeft (deviceWidth));
    row1.removeFromLeft (8);

    place (rateTitle, row1, 88);
    place (rateCombo, row1, 105);
    place (bufferTitle, row1, 56);
    bufferCombo.setBounds (row1.removeFromLeft (105));

    // 2行目: [Ref] [Meas] [出力] の各割当と、右側に3本のメーター
    place (refTitle, row2, 32);
    place (refCombo, row2, 150);
    place (measTitle, row2, 42);
    place (measCombo, row2, 150);
    place (outTitle, row2, 36);
    place (outCombo, row2, 150);

    auto metersArea = row2;
    const auto meterWidth = juce::jmin (130, (metersArea.getWidth() - 16) / 3);
    outMeter.setBounds (metersArea.removeFromRight (meterWidth));
    metersArea.removeFromRight (10);
    measMeter.setBounds (metersArea.removeFromRight (meterWidth));
    metersArea.removeFromRight (10);
    refMeter.setBounds (metersArea.removeFromRight (meterWidth));
}

void TopBar::comboBoxChanged (juce::ComboBox* combo)
{
    if (updating)
        return;

    if (combo == &deviceCombo)
    {
        const auto error = engine.selectDevice (deviceCombo.getText());

        if (error.isNotEmpty() && onError)
            onError (error);

        rebuildAll();
        return;
    }

    if (combo == &rateCombo)
    {
        const auto rates = engine.getAvailableSampleRates();
        const auto id = rateCombo.getSelectedId();

        if (id > 0 && id <= rates.size())
        {
            const auto error = engine.applyRateAndBuffer (rates[id - 1], -1);

            if (error.isNotEmpty() && onError)
                onError (error);
        }

        rebuildAll();
        return;
    }

    if (combo == &bufferCombo)
    {
        const auto sizes = engine.getAvailableBufferSizes();
        const auto id = bufferCombo.getSelectedId();

        if (id > 0 && id <= sizes.size())
        {
            const auto error = engine.applyRateAndBuffer (-1.0, sizes[id - 1]);

            if (error.isNotEmpty() && onError)
                onError (error);
        }

        rebuildAll();
        return;
    }

    if (combo == &refCombo)
    {
        engine.setRefChannel (channelForComboId (refCombo.getSelectedId()));
        engine.saveSettings();
    }
    else if (combo == &measCombo)
    {
        engine.setMeasChannel (channelForComboId (measCombo.getSelectedId()));
        engine.saveSettings();
    }
    else if (combo == &outCombo)
    {
        engine.setOutputChannel (channelForComboId (outCombo.getSelectedId()));
        engine.saveSettings();
    }
}

void TopBar::rebuildAll()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    lastGeneration = engine.getDeviceGeneration();

    // デバイス
    deviceCombo.clear (juce::dontSendNotification);
    const auto names = engine.getDeviceNames();

    for (int i = 0; i < names.size(); ++i)
        deviceCombo.addItem (names[i], i + 1);

    const auto selectedDevice = engine.getSelectedDeviceName();
    int deviceIndex = names.indexOf (selectedDevice);

    if (deviceIndex < 0 && names.size() > 0)
        deviceIndex = 0;

    if (deviceIndex >= 0)
        deviceCombo.setSelectedItemIndex (deviceIndex, juce::dontSendNotification);

    deviceCombo.setTextWhenNothingSelected (strings::noDeviceInfo);

    rebuildRateCombo();
    rebuildBufferCombo();

    juce::Array<int> indices;
    juce::StringArray channelNames;

    engine.getOpenedInputChannels (indices, channelNames);
    rebuildChannelCombo (refCombo, indices, channelNames, engine.getRefChannel());
    rebuildChannelCombo (measCombo, indices, channelNames, engine.getMeasChannel());

    engine.getOpenedOutputChannels (indices, channelNames);
    rebuildChannelCombo (outCombo, indices, channelNames, engine.getOutputChannel());
}

void TopBar::rebuildRateCombo()
{
    rateCombo.clear (juce::dontSendNotification);
    const auto rates = engine.getAvailableSampleRates();
    const auto currentRate = (int) engine.getCurrentSampleRate();
    int selectedId = 0;

    for (int i = 0; i < rates.size(); ++i)
    {
        rateCombo.addItem (juce::String ((int) rates[i]) + " Hz", i + 1);

        if ((int) rates[i] == currentRate)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        rateCombo.setSelectedId (selectedId, juce::dontSendNotification);

    rateCombo.setTextWhenNothingSelected (strings::notSelected);
}

void TopBar::rebuildBufferCombo()
{
    bufferCombo.clear (juce::dontSendNotification);
    const auto sizes = engine.getAvailableBufferSizes();
    const auto currentSize = engine.getCurrentBufferSize();
    int selectedId = 0;

    for (int i = 0; i < sizes.size(); ++i)
    {
        bufferCombo.addItem (juce::String (sizes[i]), i + 1);

        if (sizes[i] == currentSize)
            selectedId = i + 1;
    }

    if (selectedId > 0)
        bufferCombo.setSelectedId (selectedId, juce::dontSendNotification);

    bufferCombo.setTextWhenNothingSelected (strings::notSelected);
}

void TopBar::rebuildChannelCombo (juce::ComboBox& combo, const juce::Array<int>& indices,
                                  const juce::StringArray& names, int selected)
{
    combo.clear (juce::dontSendNotification);
    combo.addItem (strings::channelNone, 1);

    for (auto i = 0; i < indices.size(); ++i)
        combo.addItem (names[i], comboIdForChannel (indices[i]));

    combo.setSelectedId (comboIdForChannel (selected), juce::dontSendNotification);
}

} // namespace eeq::ui
