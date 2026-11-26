#pragma once

#include "Theme.h"

#include "Core/JuceModules.h"

// ダーク/ライト両テーマで使う共通のウィジェットスタイル（仕様 5.5）

namespace eeq::ui
{

// スタイル適用済みの目印（再スタイル用）。テーマ切替時にこの目印を持つ
// ウィジェットへ style* を再適用する（issue #9）
constexpr const char* styleMarkerProperty = "eeqStyle";
constexpr const char* styleParamProperty = "eeqStyleParam";

inline void styleButton (juce::TextButton& button, juce::Colour onColour = juce::Colour(),
                         const char* variantTag = "plain")
{
    button.getProperties().set (styleMarkerProperty, "button");
    button.getProperties().set (styleParamProperty, juce::var (variantTag));
    button.setColour (juce::TextButton::buttonColourId,
                      onColour == juce::Colour() ? theme::get (theme::ColourId::buttonOff) : onColour);
    button.setColour (juce::TextButton::textColourOffId, theme::get (theme::ColourId::text));
    button.setWantsKeyboardFocus (false);
    button.setMouseClickGrabsKeyboardFocus (false);
}

inline void styleDangerButton (juce::TextButton& button)
{
    styleButton (button, theme::get (theme::ColourId::danger), "danger");
}

inline void styleAccentButton (juce::TextButton& button)
{
    styleButton (button, theme::get (theme::ColourId::buttonOn), "accent");
}

inline void styleCombo (juce::ComboBox& combo)
{
    combo.getProperties().set (styleMarkerProperty, "combo");
    combo.setColour (juce::ComboBox::backgroundColourId, theme::get (theme::ColourId::control));
    combo.setColour (juce::ComboBox::outlineColourId, theme::get (theme::ColourId::controlOutline));
    combo.setColour (juce::ComboBox::textColourId, theme::get (theme::ColourId::text));
    combo.setColour (juce::ComboBox::arrowColourId, theme::get (theme::ColourId::textDim));
    combo.setWantsKeyboardFocus (false);
}

inline void styleLabel (juce::Label& label, float fontHeight = 12.0f, bool dim = true)
{
    label.getProperties().set (styleMarkerProperty, "label");
    label.getProperties().set (styleParamProperty,
                               juce::var (juce::String (fontHeight) + "|" + (dim ? "1" : "0")));
    label.setColour (juce::Label::textColourId,
                     theme::get (dim ? theme::ColourId::textDim : theme::ColourId::text));
    label.setFont (juce::Font (juce::FontOptions (fontHeight)));
}

/** 状態を色で示すラベル（warn/danger/accent/ok など）用。
    テーマ切替時も指定の ColourId で塗り直される（issue #50） */
inline void styleLabelWithColour (juce::Label& label, theme::ColourId textColourId,
                                  float fontHeight = 12.0f, bool bold = false)
{
    label.getProperties().set (styleMarkerProperty, "labelColour");
    label.getProperties().set (styleParamProperty,
                               juce::var (juce::String ((int) textColourId) + "|"
                                          + juce::String (fontHeight) + "|" + (bold ? "1" : "0")));
    label.setColour (juce::Label::textColourId, theme::get (textColourId));
    label.setFont (juce::Font (juce::FontOptions (fontHeight, bold ? juce::Font::bold : juce::Font::plain)));
}

inline void styleTextEditor (juce::TextEditor& editor)
{
    editor.getProperties().set (styleMarkerProperty, "editor");
    editor.setColour (juce::TextEditor::backgroundColourId, theme::get (theme::ColourId::control));
    editor.setColour (juce::TextEditor::outlineColourId, theme::get (theme::ColourId::controlOutline));
    editor.setColour (juce::TextEditor::textColourId, theme::get (theme::ColourId::text));
    editor.setColour (juce::TextEditor::highlightedTextColourId, theme::get (theme::ColourId::text));
    editor.setWantsKeyboardFocus (true);
}

inline void styleToggle (juce::ToggleButton& toggle)
{
    toggle.getProperties().set (styleMarkerProperty, "toggle");
    toggle.setColour (juce::ToggleButton::textColourId, theme::get (theme::ColourId::textDim));
    toggle.setColour (juce::ToggleButton::tickColourId, theme::get (theme::ColourId::accent));
    toggle.setColour (juce::ToggleButton::tickDisabledColourId, theme::get (theme::ColourId::textDim));
    toggle.setWantsKeyboardFocus (false);
}

inline void styleSlider (juce::Slider& slider)
{
    slider.getProperties().set (styleMarkerProperty, "slider");
    slider.setColour (juce::Slider::backgroundColourId, theme::get (theme::ColourId::control));
    slider.setColour (juce::Slider::trackColourId, theme::get (theme::ColourId::accent));
    slider.setColour (juce::Slider::thumbColourId, theme::get (theme::ColourId::text));
    slider.setColour (juce::Slider::textBoxTextColourId, theme::get (theme::ColourId::text));
    slider.setColour (juce::Slider::textBoxBackgroundColourId, theme::get (theme::ColourId::control));
    slider.setColour (juce::Slider::textBoxOutlineColourId, theme::get (theme::ColourId::controlOutline));
}

/** コンポーネント1個へスタイルを再適用する（issue #9 / #50）。
    目印があるものはそのスタイルを再適用し、目印が無い標準ウィジェットにも
    テーマの標準色を再適用する（生成時に焼き込まれた色を残さない） */
inline void restyleComponent (juce::Component& component)
{
    const auto& properties = component.getProperties();
    const auto marked = properties.contains (styleMarkerProperty);
    const auto marker = marked ? properties.getWithDefault (styleMarkerProperty, {}).toString()
                               : juce::String();

    if (auto* button = dynamic_cast<juce::TextButton*> (&component))
    {
        if (marked && marker == "button")
        {
            const auto param = properties.getWithDefault (styleParamProperty, juce::var ("plain")).toString();

            if (param == "danger")
                styleDangerButton (*button);
            else if (param == "accent")
                styleAccentButton (*button);
            else
                styleButton (*button);
        }

        return;
    }

    if (auto* combo = dynamic_cast<juce::ComboBox*> (&component))
    {
        if (! marked || marker == "combo")
            styleCombo (*combo);

        return;
    }

    if (auto* editor = dynamic_cast<juce::TextEditor*> (&component))
    {
        if (! marked || marker == "editor")
            styleTextEditor (*editor);

        return;
    }

    if (auto* toggle = dynamic_cast<juce::ToggleButton*> (&component))
    {
        if (! marked || marker == "toggle")
            styleToggle (*toggle);

        return;
    }

    if (auto* slider = dynamic_cast<juce::Slider*> (&component))
    {
        if (! marked || marker == "slider")
            styleSlider (*slider);

        return;
    }

    if (auto* list = dynamic_cast<juce::ListBox*> (&component))
    {
        list->setColour (juce::ListBox::backgroundColourId, theme::get (theme::ColourId::panelBackground));
        return;
    }

    if (auto* label = dynamic_cast<juce::Label*> (&component))
    {
        if (marked && marker == "labelColour")
        {
            // 色指定ラベル（warn/danger/accent など）は指定の ColourId で塗り直す
            const auto param = properties.getWithDefault (styleParamProperty, juce::var ("0|12|0")).toString();
            const auto first = param.indexOfChar ('|');
            const auto second = param.indexOfChar (first + 1, '|');
            const auto colourId = first > 0 ? param.upToFirstOccurrenceOf ("|", false, false).getIntValue()
                                            : (int) theme::ColourId::text;
            const auto fontHeight = second > first ? (float) param.substring (first + 1, second).getDoubleValue()
                                                   : 12.0f;
            const auto bold = second > first ? param.fromLastOccurrenceOf ("|", false, false) == "1" : false;
            styleLabelWithColour (*label, (theme::ColourId) colourId, fontHeight, bold);
            return;
        }

        if (marked && marker == "label")
        {
            const auto param = properties.getWithDefault (styleParamProperty, juce::var ("12|1")).toString();
            const auto separator = param.indexOfChar ('|');
            const auto fontHeight = separator > 0 ? (float) param.upToFirstOccurrenceOf ("|", false, false).getDoubleValue()
                                                  : 12.0f;
            const auto dim = separator > 0 ? param.fromLastOccurrenceOf ("|", false, false) == "1" : true;
            styleLabel (*label, fontHeight, dim);
            return;
        }

        // 目印が無いラベル: 標準の文字色で塗り直す（生成時に焼き込まれたダーク用の
        // 色が残ってライトテーマで読めなくなるのを防ぐ。フォントは触らない）
        label->setColour (juce::Label::textColourId, theme::get (theme::ColourId::text));
        return;
    }
}

/** コンポーネントツリー全体へスタイルを再適用する（テーマ切替時に呼ぶ。issue #9） */
inline void restyleTree (juce::Component* root)
{
    if (root == nullptr)
        return;

    restyleComponent (*root);

    for (auto* child : root->getChildren())
        restyleTree (child);
}

} // namespace eeq::ui
