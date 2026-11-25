#pragma once

#include "Theme.h"

#include <JuceHeader.h>

// ダーク/ライト両テーマで使う共通のウィジェットスタイル（仕様 5.5）

namespace eeq::ui
{

inline void styleButton (juce::TextButton& button, juce::Colour onColour = juce::Colour())
{
    button.setColour (juce::TextButton::buttonColourId,
                      onColour == juce::Colour() ? theme::get (theme::ColourId::buttonOff) : onColour);
    button.setColour (juce::TextButton::textColourOffId, theme::get (theme::ColourId::text));
    button.setWantsKeyboardFocus (false);
    button.setMouseClickGrabsKeyboardFocus (false);
}

inline void styleDangerButton (juce::TextButton& button)
{
    styleButton (button, juce::Colour (0xff8c3a3a));
}

inline void styleAccentButton (juce::TextButton& button)
{
    styleButton (button, juce::Colour (0xff2e5a63));
}

inline void styleCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, theme::get (theme::ColourId::control));
    combo.setColour (juce::ComboBox::outlineColourId, theme::get (theme::ColourId::controlOutline));
    combo.setColour (juce::ComboBox::textColourId, theme::get (theme::ColourId::text));
    combo.setColour (juce::ComboBox::arrowColourId, theme::get (theme::ColourId::textDim));
    combo.setWantsKeyboardFocus (false);
}

inline void styleLabel (juce::Label& label, float fontHeight = 12.0f, bool dim = true)
{
    label.setColour (juce::Label::textColourId,
                     theme::get (dim ? theme::ColourId::textDim : theme::ColourId::text));
    label.setFont (juce::Font (juce::FontOptions (fontHeight)));
}

inline void styleTextEditor (juce::TextEditor& editor)
{
    editor.setColour (juce::TextEditor::backgroundColourId, theme::get (theme::ColourId::control));
    editor.setColour (juce::TextEditor::outlineColourId, theme::get (theme::ColourId::controlOutline));
    editor.setColour (juce::TextEditor::textColourId, theme::get (theme::ColourId::text));
    editor.setColour (juce::TextEditor::highlightedTextColourId, theme::get (theme::ColourId::text));
    editor.setWantsKeyboardFocus (true);
}

inline void styleToggle (juce::ToggleButton& toggle)
{
    toggle.setColour (juce::ToggleButton::textColourId, theme::get (theme::ColourId::textDim));
    toggle.setColour (juce::ToggleButton::tickColourId, theme::get (theme::ColourId::accent));
    toggle.setColour (juce::ToggleButton::tickDisabledColourId, theme::get (theme::ColourId::textDim));
    toggle.setWantsKeyboardFocus (false);
}

inline void styleSlider (juce::Slider& slider)
{
    slider.setColour (juce::Slider::backgroundColourId, theme::get (theme::ColourId::control));
    slider.setColour (juce::Slider::trackColourId, theme::get (theme::ColourId::accent));
    slider.setColour (juce::Slider::thumbColourId, theme::get (theme::ColourId::text));
    slider.setColour (juce::Slider::textBoxTextColourId, theme::get (theme::ColourId::text));
    slider.setColour (juce::Slider::textBoxBackgroundColourId, theme::get (theme::ColourId::control));
    slider.setColour (juce::Slider::textBoxOutlineColourId, theme::get (theme::ColourId::controlOutline));
}

} // namespace eeq::ui
