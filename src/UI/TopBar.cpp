#include "TopBar.h"

#include "Buttons.h"
#include "Strings.h"

namespace eeq::ui
{

TopBar::TopBar (eeq::audio::AudioEngine& engineIn)
    : engine (engineIn),
      refMeter (strings::refLabel),
      measMeter (strings::measLabel),
      outMeter (strings::outLabel)
{
    inputDeviceTitle.setText (strings::inputDeviceLabel, juce::dontSendNotification);
    outputDeviceTitle.setText (strings::outputDeviceLabel, juce::dontSendNotification);
    rateTitle.setText (strings::sampleRateLabel, juce::dontSendNotification);
    bufferTitle.setText (strings::bufferSizeLabel, juce::dontSendNotification);
    refTitle.setText (strings::refLabel, juce::dontSendNotification);
    measTitle.setText (strings::measLabel, juce::dontSendNotification);
    outTitle.setText (strings::outLabel, juce::dontSendNotification);

    for (auto* label : { &inputDeviceTitle, &outputDeviceTitle, &rateTitle, &bufferTitle, &refTitle, &measTitle, &outTitle })
    {
        styleLabel (*label, 13.0f);
        addAndMakeVisible (label);
    }

    for (auto* combo : { &inputDeviceCombo, &outputDeviceCombo, &rateCombo, &bufferCombo, &refCombo, &measCombo, &outCombo })
    {
        styleCombo (*combo);
        combo->addListener (this);
        addAndMakeVisible (combo);
    }

    for (auto* meter : { &refMeter, &measMeter, &outMeter })
        addAndMakeVisible (meter);

    // メーターのクリックでクリップ表示を解除する（issue #146）。R（平均リセット）でも解除される
    for (auto* meter : { &refMeter, &measMeter, &outMeter })
        meter->onClick = [this] { engine.clearClipLatches(); };

    // 本番モード（5.4）へのワンボタン切替
    styleAccentButton (liveModeButton);
    liveModeButton.setButtonText (strings::tabLiveMode);
    liveModeButton.onClick = [this]
    {
        if (onLiveModeRequested != nullptr)
            onLiveModeRequested();
    };
    addAndMakeVisible (liveModeButton);

    // ダーク/ライト切替（仕様 5.）
    styleButton (themeButton);
    themeButton.setButtonText (strings::themeToggle);
    themeButton.onClick = [this]
    {
        if (onThemeToggleRequested != nullptr)
            onThemeToggleRequested();
    };
    addAndMakeVisible (themeButton);

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
    // 「準備」画面のカード（2026-11 リデザイン）: 左に機器、右にチャンネル割当とレベル
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::get (theme::ColourId::raised));
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.drawRoundedRectangle (bounds, 10.0f, 1.0f);

    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (strings::setupDevicesTitle, devicesTitleArea, juce::Justification::centredLeft);
    g.drawText (strings::setupChannelsTitle, channelsTitleArea, juce::Justification::centredLeft);
    g.drawText (strings::setupLevelsTitle, levelsTitleArea, juce::Justification::centredLeft);

    g.setColour (theme::get (theme::ColourId::controlOutline));
    if (! dividerX.empty())
        for (auto x : dividerX)
            g.fillRect (x, 18, 1, getHeight() - 36);
}

void TopBar::resized()
{
    auto bounds = getLocalBounds().reduced (20, 16);
    dividerX.clear();

    // 3 列: 機器 | チャンネル | レベル
    const auto columnWidth = (bounds.getWidth() - 2 * 41) / 3;
    auto devices = bounds.removeFromLeft (columnWidth);
    dividerX.push_back (bounds.getX() + 20);
    bounds.removeFromLeft (41);
    auto channels = bounds.removeFromLeft (columnWidth);
    dividerX.push_back (bounds.getX() + 20);
    bounds.removeFromLeft (41);
    auto levels = bounds;

    auto row = [] (juce::Rectangle<int>& column, juce::Label& title, juce::Component& control)
    {
        auto r = column.removeFromTop (32);
        title.setBounds (r.removeFromLeft (96));
        control.setBounds (r.withSizeKeepingCentre (r.getWidth(), 30));
        column.removeFromTop (8);
    };

    devicesTitleArea = devices.removeFromTop (26);
    devices.removeFromTop (8);
    row (devices, inputDeviceTitle, inputDeviceCombo);
    row (devices, outputDeviceTitle, outputDeviceCombo);
    row (devices, rateTitle, rateCombo);
    row (devices, bufferTitle, bufferCombo);

    channelsTitleArea = channels.removeFromTop (26);
    channels.removeFromTop (8);
    row (channels, refTitle, refCombo);
    row (channels, measTitle, measCombo);
    row (channels, outTitle, outCombo);

    levelsTitleArea = levels.removeFromTop (26);
    levels.removeFromTop (12);
    for (auto* meter : { &refMeter, &measMeter, &outMeter })
    {
        meter->setBounds (levels.removeFromTop (30));
        levels.removeFromTop (14);
    }

    // 本番モード・テーマの切替は上部のステータス帯と左のナビへ移した
    liveModeButton.setVisible (false);
    themeButton.setVisible (false);
}

void TopBar::comboBoxChanged (juce::ComboBox* combo)
{
    if (updating)
        return;

    if (combo == &inputDeviceCombo)
    {
        const auto error = engine.selectInputDevice (inputDeviceCombo.getText());

        if (error.isNotEmpty() && onError)
            onError (error);

        rebuildAll();
        return;
    }

    if (combo == &outputDeviceCombo)
    {
        const auto error = engine.selectOutputDevice (outputDeviceCombo.getText());

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
        engine.setRefChannel (channelForComboId (refCombo.getSelectedId(), true));
        engine.saveSettings();
    }
    else if (combo == &measCombo)
    {
        engine.setMeasChannel (channelForComboId (measCombo.getSelectedId(), false));
        engine.saveSettings();
    }
    else if (combo == &outCombo)
    {
        engine.setOutputChannel (channelForComboId (outCombo.getSelectedId(), false));
        engine.saveSettings();
    }
}

void TopBar::rebuildAll()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    lastGeneration = engine.getDeviceGeneration();

    // デバイス（入力と出力は別々の一覧）
    fillDeviceCombo (inputDeviceCombo, engine.getAvailableDeviceNames (true), engine.getSelectedInputDeviceName());
    fillDeviceCombo (outputDeviceCombo, engine.getAvailableDeviceNames (false), engine.getSelectedOutputDeviceName());

    rebuildRateCombo();
    rebuildBufferCombo();

    juce::Array<int> indices;
    juce::StringArray channelNames;

    engine.getOpenedInputChannels (indices, channelNames);
    rebuildChannelCombo (refCombo, true, indices, channelNames, engine.getRefChannel());
    rebuildChannelCombo (measCombo, false, indices, channelNames, engine.getMeasChannel());

    engine.getOpenedOutputChannels (indices, channelNames);
    rebuildChannelCombo (outCombo, false, indices, channelNames, engine.getOutputChannel());
}

void TopBar::fillDeviceCombo (juce::ComboBox& combo, const juce::StringArray& names, const juce::String& selectedName)
{
    combo.clear (juce::dontSendNotification);

    for (int i = 0; i < names.size(); ++i)
        combo.addItem (names[i], i + 1);

    auto selectedIndex = names.indexOf (selectedName);

    if (selectedIndex < 0 && names.size() > 0)
        selectedIndex = 0;

    if (selectedIndex >= 0)
        combo.setSelectedItemIndex (selectedIndex, juce::dontSendNotification);

    combo.setTextWhenNothingSelected (strings::noDeviceInfo);
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

void TopBar::rebuildChannelCombo (juce::ComboBox& combo, bool allowInternal, const juce::Array<int>& indices,
                                  const juce::StringArray& names, int selected)
{
    combo.clear (juce::dontSendNotification);

    if (allowInternal)
        combo.addItem (strings::internalReference, 1);

    combo.addItem (strings::channelNone, allowInternal ? 2 : 1);

    for (auto i = 0; i < indices.size(); ++i)
        combo.addItem (names[i], comboIdForChannel (indices[i], allowInternal));

    combo.setSelectedId (comboIdForChannel (selected, allowInternal), juce::dontSendNotification);
}

} // namespace eeq::ui
