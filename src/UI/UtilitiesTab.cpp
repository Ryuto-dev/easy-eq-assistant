#include "UtilitiesTab.h"

#include "../Core/Acoustics.h"
#include "../Core/Utilities.h"
#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

UtilitiesTab::UtilitiesTab (UiContext& context)
    : ctx (context)
{
    auto setupEditor = [this] (juce::TextEditor& editor, const juce::String& initial)
    {
        styleTextEditor (editor);
        editor.setText (initial, juce::dontSendNotification);
        editor.setInputRestrictions (12, "0123456789.-");
        editor.addListener (this);
        addAndMakeVisible (editor);
    };

    auto setupLabel = [this] (juce::Label& label, const juce::String& text, bool bold = false)
    {
        label.setText (text, juce::dontSendNotification);
        styleLabel (label, bold ? 12.5f : 11.5f, ! bold);
        addAndMakeVisible (label);
    };

    // ---- 距離⇄遅延 ----
    setupLabel (distanceDelayTitle, strings::toolDistanceDelay, true);
    setupLabel (distanceLabel, strings::toolMetres);
    setupLabel (delayLabel2, strings::msUnit);
    setupEditor (distanceEditor, "30");
    setupEditor (delayEditor, "87.36");
    setupLabel (distanceResultLabel, {});
    setupLabel (delayResultLabel, {});

    // ---- 音速 ----
    setupLabel (temperatureTitle, strings::toolSpeedOfSound, true);
    setupLabel (temperatureEditorLabel, strings::temperatureLabel + " (℃)");
    setupEditor (temperatureEditor, "20");
    setupLabel (soundSpeedLabel, {});
    temperatureNoteLabel.setText (strings::temperatureManualNote, juce::dontSendNotification);
    styleLabel (temperatureNoteLabel, 10.5f);
    addAndMakeVisible (temperatureNoteLabel);

    // ---- 周波数⇄波長 ----
    setupLabel (wavelengthTitle, strings::toolWavelength, true);
    setupLabel (wavelengthFreqLabel, strings::toolHz);
    setupEditor (wavelengthFreqEditor, "100");
    setupLabel (wavelengthResultLabel, {});

    // ---- コムフィルタ ----
    setupLabel (combTitle, strings::toolCombFilter, true);
    setupLabel (combDelayLabel, strings::delayLabel + " (ms)");
    setupEditor (combDelayEditor, "5");
    setupLabel (combResultLabel, {});

    // ---- 距離減衰 ----
    setupLabel (attenuationTitle, strings::toolDistanceAttenuation, true);
    setupLabel (attenFromLabel, strings::toolMetres);
    setupLabel (attenToLabel, strings::toolMetres);
    setupEditor (attenFromEditor, "10");
    setupEditor (attenToEditor, "20");
    setupLabel (attenResultLabel, {});

    // ---- レベル換算 ----
    setupLabel (levelTitle, strings::toolLevelConversion, true);
    setupLabel (voltsLabel, "V");
    setupEditor (voltsEditor, "1");
    setupLabel (levelResultLabel, {});

    // ---- テンポ⇄時間 ----
    setupLabel (tempoTitle, strings::toolTempo, true);
    setupLabel (bpmLabel, strings::toolBpm);
    setupEditor (bpmEditor, "120");
    setupLabel (tempoResultLabel, {});

    // ---- Q⇄帯域幅 ----
    setupLabel (qbwTitle, strings::toolQBandwidth, true);
    setupLabel (qLabel, "Q");
    setupEditor (qEditor, "2");
    setupLabel (bwLabel, strings::toolOctaves);
    setupEditor (bwEditor, "1.414");
    setupLabel (qResultLabel, {});
    setupLabel (bwResultLabel, {});

    updateCalculations();
}

void UtilitiesTab::textEditorTextChanged (juce::TextEditor&)
{
    updateCalculations();
}

void UtilitiesTab::updateCalculations()
{
    const auto temperature = temperatureEditor.getText().getDoubleValue();

    // 気温は全アプリで共有する（仕様 3.3.2 / 3.8: 既定 20 ℃）
    if (temperature > -20.0 && temperature < 60.0)
        ctx.state.airTemperatureC.store (temperature);

    const auto soundSpeed = eeq::acoustics::speedOfSoundMetresPerSecond (temperature);
    soundSpeedLabel.setText (juce::String (soundSpeed, 2) + " m/s", juce::dontSendNotification);

    // 距離⇄遅延（相互に連動）
    const auto metres = distanceEditor.getText().getDoubleValue();
    const auto ms = delayEditor.getText().getDoubleValue();
    distanceResultLabel.setText (juce::String (eeq::acoustics::delayMillisecondsForDistance (metres, temperature), 2)
                                     + " " + strings::msUnit,
                                 juce::dontSendNotification);
    delayResultLabel.setText (juce::String (eeq::acoustics::distanceMetresForDelay (ms, temperature), 2)
                                  + " " + strings::toolMetres,
                              juce::dontSendNotification);

    // 周波数⇄波長（波長・半波長・1/4波長。仕様 5.6）
    {
        const auto freq = wavelengthFreqEditor.getText().getDoubleValue();

        if (freq > 0.0)
        {
            const auto lambda = eeq::util::wavelengthMetres (freq, temperature);
            wavelengthResultLabel.setText (juce::String (lambda, 3) + " m / "
                                               + juce::String (lambda / 2.0, 3) + " m (" + strings::toolHalfWave + ") / "
                                               + juce::String (lambda / 4.0, 3) + " m (" + strings::toolQuarterWave + ")",
                                           juce::dontSendNotification);
        }
        else
            wavelengthResultLabel.setText ({}, juce::dontSendNotification);
    }

    // コムフィルタ（キャンセル周波数。仕様 5.6）
    {
        const auto delayMs = combDelayEditor.getText().getDoubleValue();

        if (delayMs > 0.0)
        {
            juce::String text;

            for (int k = 1; k <= 5; ++k)
                text += (k == 1 ? "" : "  ") + juce::String (eeq::util::combFilterNullFrequency (delayMs, k), 1) + " Hz";

            combResultLabel.setText (text, juce::dontSendNotification);
        }
        else
            combResultLabel.setText ({}, juce::dontSendNotification);
    }

    // 距離減衰（逆二乗則。仕様 5.6）
    {
        const auto from = attenFromEditor.getText().getDoubleValue();
        const auto to = attenToEditor.getText().getDoubleValue();
        attenResultLabel.setText (juce::String (eeq::util::distanceAttenuationDb (from, to), 2) + " dB",
                                  juce::dontSendNotification);
    }

    // レベル換算（dBu / dBV / V。仕様 5.6）
    {
        const auto volts = voltsEditor.getText().getDoubleValue();
        levelResultLabel.setText (juce::String (eeq::util::voltsToDbu (volts), 2) + " dBu / "
                                      + juce::String (eeq::util::voltsToDbv (volts), 2) + " dBV",
                                  juce::dontSendNotification);
    }

    // テンポ⇄時間
    {
        const auto bpm = bpmEditor.getText().getDoubleValue();
        tempoResultLabel.setText (juce::String (eeq::util::tempoToMilliseconds (bpm), 1) + " ms (1/4)"
                                      + "  /  " + juce::String (eeq::util::tempoToMilliseconds (bpm, 0.5), 1) + " ms (1/8)"
                                      + "  /  " + juce::String (eeq::util::tempoToMilliseconds (bpm, 2.0), 1) + " ms (1/2)",
                                  juce::dontSendNotification);
    }

    // Q⇄帯域幅（相互換算）
    {
        const auto q = qEditor.getText().getDoubleValue();
        const auto bw = bwEditor.getText().getDoubleValue();
        qResultLabel.setText (juce::String (eeq::util::qToBandwidthOctaves (q), 3) + " " + strings::toolOctaves,
                              juce::dontSendNotification);
        bwResultLabel.setText ("Q " + juce::String (eeq::util::bandwidthOctavesToQ (bw), 3),
                               juce::dontSendNotification);
    }
}

void UtilitiesTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));
}

void UtilitiesTab::resized()
{
    auto bounds = getLocalBounds().reduced (10);
    const auto columnWidth = (bounds.getWidth() - 20) / 3;
    const auto columnHeight = (bounds.getHeight() - 20) / 2;

    auto place = [&] (int column, int row, juce::Component& title, std::initializer_list<juce::Component*> others)
    {
        auto cell = juce::Rectangle<int> (bounds.getX() + column * (columnWidth + 10),
                                          bounds.getY() + row * (columnHeight + 10),
                                          columnWidth, columnHeight);
        title.setBounds (cell.removeFromTop (18));
        cell.removeFromTop (2);

        const auto controlHeight = 24;
        int i = 0;

        for (auto* component : others)
        {
            component->setBounds (cell.removeFromTop (controlHeight));
            cell.removeFromTop (2);
            ++i;
        }
    };

    place (0, 0, distanceDelayTitle, { &distanceLabel, &distanceEditor, &distanceResultLabel, &delayLabel2, &delayEditor, &delayResultLabel });
    place (1, 0, temperatureTitle, { &temperatureEditorLabel, &temperatureEditor, &soundSpeedLabel, &temperatureNoteLabel });
    place (2, 0, wavelengthTitle, { &wavelengthFreqLabel, &wavelengthFreqEditor, &wavelengthResultLabel, &combTitle, &combDelayLabel, &combDelayEditor, &combResultLabel });
    place (0, 1, attenuationTitle, { &attenFromLabel, &attenFromEditor, &attenToLabel, &attenToEditor, &attenResultLabel });
    place (1, 1, levelTitle, { &voltsLabel, &voltsEditor, &levelResultLabel, &tempoTitle, &bpmLabel, &bpmEditor, &tempoResultLabel });
    place (2, 1, qbwTitle, { &qLabel, &qEditor, &qResultLabel, &bwLabel, &bwEditor, &bwResultLabel });
}

} // namespace eeq::ui
