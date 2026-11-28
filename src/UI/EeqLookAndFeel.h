#pragma once

#include "Buttons.h"
#include "Theme.h"

#include "Core/JuceModules.h"

namespace eeq::ui
{

// アプリ全体の見た目（2026-11 リデザイン）。
// 平らな面・細い枠・角丸 6px・控えめなホバーで統一する。色はすべて theme から都度引くので、
// テーマを切り替えても作り直す必要はない（ウィジェットに焼き込んだ色は applyThemeToComponents が塗り直す）。
// ボタンの種類は styleButton の目印（eeqStyleParam: plain / accent / danger）で描き分ける。
class EeqLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr float radius = 6.0f;

    EeqLookAndFeel() { refreshColours(); }

    /** テーマ切替のあとに呼ぶ（ダイアログ・メニューなど、JUCE が色 ID で描く部品の色を更新する） */
    void refreshColours()
    {
        using theme::ColourId;
        using CS = juce::LookAndFeel_V4::ColourScheme;
        setColourScheme (CS (theme::get (ColourId::windowBackground), theme::get (ColourId::panelBackground),
                             theme::get (ColourId::raised), theme::get (ColourId::controlOutline),
                             theme::get (ColourId::text), theme::get (ColourId::control), theme::get (ColourId::text),
                             theme::get (ColourId::accent).withAlpha (0.25f), theme::get (ColourId::text)));

        setColour (juce::TooltipWindow::backgroundColourId, theme::get (ColourId::raised));
        setColour (juce::TooltipWindow::textColourId, theme::get (ColourId::text));
        setColour (juce::TooltipWindow::outlineColourId, theme::get (ColourId::controlOutline));
        setColour (juce::AlertWindow::backgroundColourId, theme::get (ColourId::panelBackground));
        setColour (juce::AlertWindow::textColourId, theme::get (ColourId::text));
        setColour (juce::AlertWindow::outlineColourId, theme::get (ColourId::controlOutline));
        setColour (juce::PopupMenu::backgroundColourId, theme::get (ColourId::raised));
        setColour (juce::PopupMenu::textColourId, theme::get (ColourId::text));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::get (ColourId::accentSoft));
        setColour (juce::PopupMenu::highlightedTextColourId, theme::get (ColourId::text));
        setColour (juce::ListBox::backgroundColourId, theme::get (ColourId::panelBackground));
        setColour (juce::ListBox::outlineColourId, theme::get (ColourId::controlOutline));
        setColour (juce::ScrollBar::thumbColourId, theme::get (ColourId::gridMajor));
        setColour (juce::TextEditor::focusedOutlineColourId, theme::get (ColourId::accent));
        setColour (juce::CaretComponent::caretColourId, theme::get (ColourId::accent));
        setColour (juce::TabbedButtonBar::tabTextColourId, theme::get (ColourId::textDim));
        setColour (juce::TabbedButtonBar::frontTextColourId, theme::get (ColourId::text));
    }

    //==============================================================================
    // 文字

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jlimit (11.5f, 15.0f, (float) buttonHeight * 0.48f)));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (juce::FontOptions (juce::jlimit (11.5f, 14.0f, (float) box.getHeight() * 0.5f)));
    }

    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (13.5f)); }

    juce::Font getAlertWindowMessageFont() override { return juce::Font (juce::FontOptions (14.0f)); }

    //==============================================================================
    // ボタン

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto variant = button.getProperties()[styleParamProperty].toString();
        const auto enabled = button.isEnabled();

        juce::Colour fill, outline;

        if (variant == "danger")
        {
            const auto danger = theme::get (theme::ColourId::danger);
            fill = danger.withAlpha (button.getToggleState() ? 0.35f : 0.12f);
            outline = danger.withAlpha (0.65f);
        }
        else if (variant == "accent" || variant == "primary")
        {
            const auto accent = theme::get (theme::ColourId::accent);
            const bool solid = variant == "primary" || button.getToggleState();
            fill = solid ? accent : accent.withAlpha (0.12f);
            outline = accent.withAlpha (solid ? 1.0f : 0.45f);
        }
        else if (variant == "ghost")
        {
            // ナビの下などの控えめなボタン: 普段は枠だけ薄く、ホバーで面を出す
            fill = juce::Colours::transparentBlack;
            outline = theme::get (theme::ColourId::controlOutline).withAlpha (0.7f);
        }
        else
        {
            // 呼び出し側が状態色（測定中など）を渡したときはそれを面の色にする
            const auto plain = theme::get (theme::ColourId::buttonOff);
            fill = backgroundColour == plain || backgroundColour.isTransparent() ? theme::get (theme::ColourId::raised)
                                                                                 : backgroundColour;
            outline = theme::get (theme::ColourId::controlOutline);
        }

        if (enabled && highlighted && variant == "ghost")
            fill = theme::get (theme::ColourId::hover);
        else if (enabled && highlighted)
            fill = fill.overlaidWith (theme::get (theme::ColourId::text).withAlpha (0.05f));
        if (enabled && down)
            fill = fill.overlaidWith (juce::Colours::black.withAlpha (0.12f));
        if (! enabled)
        {
            fill = fill.withMultipliedAlpha (0.5f);
            outline = outline.withMultipliedAlpha (0.5f);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (outline);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const auto variant = button.getProperties()[styleParamProperty].toString();
        auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                  : juce::TextButton::textColourOffId);

        if (variant == "danger")
            colour = theme::get (theme::ColourId::danger);
        else if (variant == "primary" || (variant == "accent" && button.getToggleState()))
            colour = theme::get (theme::ColourId::onAccent);
        else if (variant == "accent")
            colour = theme::get (theme::ColourId::accent);
        else if (variant == "ghost")
            colour = theme::get (theme::ColourId::textDim);

        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.45f);

        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (colour);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 2), juce::Justification::centred,
                          2, 0.85f);
    }

    //==============================================================================
    // トグル（チェックボックス）

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto h = (float) button.getHeight();
        const auto box = juce::Rectangle<float> (2.0f, (h - 16.0f) * 0.5f, 16.0f, 16.0f);
        const auto on = button.getToggleState();
        const auto accent = theme::get (theme::ColourId::accent);

        g.setColour (on ? accent : theme::get (theme::ColourId::control));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (on ? accent : theme::get (highlighted ? theme::ColourId::textDim : theme::ColourId::controlOutline));
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);

        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
            tick.lineTo (box.getX() + 7.0f, box.getBottom() - 4.5f);
            tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
            g.setColour (theme::get (theme::ColourId::onAccent));
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        auto colour = button.findColour (juce::ToggleButton::textColourId);
        if (on)
            colour = theme::get (theme::ColourId::text);
        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.5f);

        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (11.5f, 14.0f, h * 0.5f))));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (24), juce::Justification::centredLeft,
                          2, 0.85f);
    }

    //==============================================================================
    // コンボボックス

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        const auto hover = box.isMouseOver (true) && box.isEnabled();

        g.setColour (hover ? theme::get (theme::ColourId::hover) : theme::get (theme::ColourId::control));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (box.hasKeyboardFocus (false) ? theme::get (theme::ColourId::accent)
                                                  : theme::get (theme::ColourId::controlOutline));
        g.drawRoundedRectangle (bounds, radius, 1.0f);

        // 下向きのシェブロン
        const auto cx = (float) width - 14.0f;
        const auto cy = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (theme::get (theme::ColourId::textDim).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    //==============================================================================
    // スライダー（横）

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }

        const auto track = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 2.0f, (float) width, 4.0f);
        g.setColour (theme::get (theme::ColourId::control).overlaidWith (theme::get (theme::ColourId::text).withAlpha (0.06f)));
        g.fillRoundedRectangle (track, 2.0f);

        const auto fillColour = slider.findColour (juce::Slider::trackColourId);
        g.setColour (fillColour);
        g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

        const auto thumb = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ sliderPos, track.getCentreY() });
        g.setColour (theme::get (theme::ColourId::text));
        g.fillEllipse (thumb);
        g.setColour (fillColour);
        g.drawEllipse (thumb.reduced (1.0f), 2.0f);
    }

    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    //==============================================================================
    // テキスト入力

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), radius);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (! editor.isEnabled())
            return;

        g.setColour (editor.hasKeyboardFocus (true) && ! editor.isReadOnly()
                         ? theme::get (theme::ColourId::accent)
                         : editor.findColour (juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), radius, 1.0f);
    }

    //==============================================================================
    // メニュー・スクロールバー

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
    {
        g.fillAll (theme::get (theme::ColourId::raised));
        g.setColour (theme::get (theme::ColourId::controlOutline));
        g.drawRect (0, 0, width, height);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool) override
    {
        juce::ignoreUnused (bar);
        const auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                    : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
        g.setColour (theme::get (theme::ColourId::gridMajor).withAlpha (mouseOver ? 1.0f : 0.7f));
        g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
    }
};

} // namespace eeq::ui
