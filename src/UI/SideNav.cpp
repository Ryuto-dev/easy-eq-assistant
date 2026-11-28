#include "SideNav.h"

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

namespace eeq::ui
{

SideNav::SideNav()
{
    styleButton (themeButton, {}, "ghost");
    themeButton.setButtonText (strings::navThemeButton);
    themeButton.onClick = [this] { if (onThemeToggle) onThemeToggle(); };
    addAndMakeVisible (themeButton);

    styleButton (helpButton, {}, "ghost");
    helpButton.setButtonText (strings::navShortcutsButton);
    helpButton.onClick = [this] { if (onShortcutHelp) onShortcutHelp(); };
    addAndMakeVisible (helpButton);
}

void SideNav::setItems (std::vector<Item> newItems)
{
    items = std::move (newItems);
    resized();
    repaint();
}

void SideNav::setSelectedPage (int pageId)
{
    if (selectedPage != pageId)
    {
        selectedPage = pageId;
        repaint();
    }
}

void SideNav::setStepState (int pageId, bool done, bool attention)
{
    for (auto& item : items)
        if (item.pageId == pageId && (item.done != done || item.attention != attention))
        {
            item.done = done;
            item.attention = attention;
            repaint();
        }
}

void SideNav::resized()
{
    layout.clear();
    auto area = getLocalBounds().reduced (10, 0);
    area.removeFromTop (14);
    flowHeader = area.removeFromTop (22);

    bool inTools = false;

    for (const auto& item : items)
    {
        if (item.stepNumber == 0 && ! inTools)
        {
            inTools = true;
            area.removeFromTop (14);
            toolsHeader = area.removeFromTop (22);
        }

        layout.push_back (area.removeFromTop (item.stepNumber > 0 ? 46 : 32));
        area.removeFromTop (2);
    }

    auto bottom = getLocalBounds().reduced (10, 12).removeFromBottom (28);
    helpButton.setBounds (bottom.removeFromRight (bottom.getWidth() / 2 - 3));
    bottom.removeFromRight (6);
    themeButton.setBounds (bottom);
}

juce::Rectangle<int> SideNav::itemBounds (size_t index) const
{
    return index < layout.size() ? layout[index] : juce::Rectangle<int>();
}

int SideNav::itemAt (juce::Point<int> position) const
{
    for (size_t i = 0; i < layout.size(); ++i)
        if (layout[i].contains (position))
            return (int) i;

    return -1;
}

void SideNav::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::sidebarBackground));
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.fillRect (getLocalBounds().removeFromRight (1));

    const auto dim = theme::get (theme::ColourId::textDim);
    const auto text = theme::get (theme::ColourId::text);
    const auto accent = theme::get (theme::ColourId::accent);

    auto drawHeader = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (dim.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (title, r.withTrimmedLeft (8), juce::Justification::centredLeft);
    };

    drawHeader (flowHeader, strings::navFlowHeader);
    if (! toolsHeader.isEmpty())
        drawHeader (toolsHeader, strings::navToolsHeader);

    for (size_t i = 0; i < items.size() && i < layout.size(); ++i)
    {
        const auto& item = items[i];
        const auto r = layout[i];
        const auto selected = item.pageId == selectedPage;

        if (selected || (int) i == hoveredIndex)
        {
            g.setColour (selected ? theme::get (theme::ColourId::accentSoft) : theme::get (theme::ColourId::hover));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }

        if (selected)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (r.toFloat().removeFromLeft (3.0f).reduced (0.0f, 8.0f), 1.5f);
        }

        auto content = r.reduced (10, 0);

        if (item.stepNumber > 0)
        {
            // 番号の丸（完了なら ✓）
            const auto circle = content.removeFromLeft (22).withSizeKeepingCentre (22, 22).toFloat();
            content.removeFromLeft (10);

            if (item.done)
            {
                g.setColour (theme::get (theme::ColourId::ok).withAlpha (0.18f));
                g.fillEllipse (circle);
                g.setColour (theme::get (theme::ColourId::ok));
                juce::Path tick;
                tick.startNewSubPath (circle.getX() + 6.5f, circle.getCentreY());
                tick.lineTo (circle.getX() + 9.5f, circle.getBottom() - 7.0f);
                tick.lineTo (circle.getRight() - 6.0f, circle.getY() + 7.0f);
                g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.setColour (selected ? accent : theme::get (theme::ColourId::controlOutline));
                g.drawEllipse (circle.reduced (0.75f), 1.5f);
                g.setColour (selected ? accent : dim);
                g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
                g.drawText (juce::String (item.stepNumber), circle, juce::Justification::centred);
            }

            auto titleRow = content.removeFromTop (content.getHeight() / 2 + 2);
            g.setColour (selected ? text : text.withAlpha (0.9f));
            g.setFont (juce::Font (juce::FontOptions (14.0f, selected ? juce::Font::bold : juce::Font::plain)));
            g.drawText (item.title, titleRow.withTrimmedTop (4), juce::Justification::bottomLeft);
            g.setColour (dim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (item.subtitle, content, juce::Justification::topLeft);
        }
        else
        {
            g.setColour (selected ? text : dim);
            g.setFont (juce::Font (juce::FontOptions (13.0f, selected ? juce::Font::bold : juce::Font::plain)));
            g.drawText (item.title, content.withTrimmedLeft (6), juce::Justification::centredLeft);
        }

        if (item.attention)
        {
            g.setColour (theme::get (theme::ColourId::warn));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) r.getRight() - 12.0f,
                                                                             (float) r.getCentreY() }));
        }
    }
}

void SideNav::mouseMove (const juce::MouseEvent& event)
{
    const auto index = itemAt (event.getPosition());

    if (index != hoveredIndex)
    {
        hoveredIndex = index;
        setMouseCursor (index >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SideNav::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIndex != -1)
    {
        hoveredIndex = -1;
        repaint();
    }
}

void SideNav::mouseUp (const juce::MouseEvent& event)
{
    const auto index = itemAt (event.getPosition());

    if (index >= 0 && index < (int) items.size() && ! event.mouseWasDraggedSinceMouseDown())
    {
        setSelectedPage (items[(size_t) index].pageId);

        if (onPageSelected)
            onPageSelected (items[(size_t) index].pageId);
    }
}

} // namespace eeq::ui
