#include "ApplyValues.h"

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include "../Core/Log.h"

namespace eeq::ui
{

//==============================================================================
namespace applyvalues
{

eeq::core::SpeakerSystemInfo& systemNamed (UiContext& ctx, const juce::String& requested)
{
    auto& systems = ctx.project.currentVenue().systems;
    const auto name = requested.isNotEmpty() ? requested : juce::String (strings::systemsAll);

    for (auto& system : systems)
        if (system.name == name)
            return system;

    eeq::core::SpeakerSystemInfo created;
    created.name = name;
    created.outputChannel = juce::jmax (0, ctx.engine.getOutputChannel());
    systems.push_back (created);
    ctx.state.snapshotVersion.fetch_add (1); // 系統の一覧を使う画面に作り直しを促す
    return systems.back();
}

void recordDelay (UiContext& ctx, const juce::String& systemName, double delayMs, const juce::String& note)
{
    auto& system = systemNamed (ctx, systemName);
    system.delaySet = true;
    system.delayMs = delayMs;
    system.entered = false;
    if (note.isNotEmpty())
        system.tuningNote = note;
    system.tuningUpdated = juce::Time::getCurrentTime();
    eeq::log::write ("apply sheet: delay " + juce::String (delayMs, 2) + " ms recorded for \"" + system.name + "\"");
}

void recordPolarity (UiContext& ctx, const juce::String& systemName, bool invert)
{
    auto& system = systemNamed (ctx, systemName);
    system.polaritySet = true;
    system.invertPolarity = invert;
    system.entered = false;
    system.tuningUpdated = juce::Time::getCurrentTime();
}

void recordEq (UiContext& ctx, const juce::String& systemName, const std::vector<eeq::dsp::peq::Band>& bands)
{
    auto& system = systemNamed (ctx, systemName);
    system.eqSet = true;
    system.eqBands = bands;
    system.entered = false;
    system.tuningUpdated = juce::Time::getCurrentTime();
    eeq::log::write ("apply sheet: " + juce::String ((int) bands.size()) + " EQ bands recorded for \"" + system.name + "\"");
}

juce::String bandTypeName (eeq::dsp::peq::Band::Type type)
{
    using T = eeq::dsp::peq::Band::Type;
    switch (type)
    {
        case T::lowShelf:  return strings::eqLowShelf;
        case T::highShelf: return strings::eqHighShelf;
        case T::lowPass:   return strings::eqLowPass;
        case T::highPass:  return strings::eqHighPass;
        case T::bell:
        default:           return strings::eqBell;
    }
}

juce::String sheetAsText (UiContext& ctx)
{
    auto& venue = ctx.project.currentVenue();
    juce::String text;
    text << strings::applySheetTitle << "\n" << venue.name << "  " << juce::Time::getCurrentTime().toString (true, true, false)
         << "\n\n";

    for (const auto& system : venue.systems)
    {
        text << "■ " << system.name << "  (" << strings::systemsChannelLabel << " " << (system.outputChannel + 1) << ")\n";
        text << "  " << strings::applyDelay << ": "
             << (system.delaySet ? juce::String (system.delayMs, 2) + " ms" : juce::String (strings::applyUndecided)) << "\n";
        text << "  " << strings::comparePolarity << ": "
             << (system.polaritySet ? (system.invertPolarity ? juce::String (strings::applyPolarityInvert)
                                                             : juce::String (strings::applyPolarityNormal))
                                    : juce::String (strings::applyUndecided)) << "\n";
        text << "  EQ: ";

        if (! system.eqSet)
            text << strings::applyUndecided << "\n";
        else if (system.eqBands.empty())
            text << strings::applyEqFlat << "\n";
        else
        {
            text << "\n";
            int index = 1;
            for (const auto& band : system.eqBands)
                text << "    " << index++ << ". " << bandTypeName (band.type) << "  " << juce::String (juce::roundToInt (band.frequency))
                     << " Hz  " << (band.gainDb >= 0 ? "+" : "") << juce::String (band.gainDb, 1) << " dB  Q "
                     << juce::String (band.q, 2) << (band.enabled ? "" : "  (off)") << "\n";
        }

        if (system.tuningNote.isNotEmpty())
            text << "  " << strings::applyNote << ": " << system.tuningNote << "\n";

        text << "  " << (system.entered ? strings::applyEnteredDone : strings::applyEnteredNot) << "\n\n";
    }

    return text;
}

} // namespace applyvalues

//==============================================================================
ApplySummaryPanel::ApplySummaryPanel (UiContext& context) : ctx (context)
{
    styleButton (openButton);
    openButton.setButtonText (strings::applyOpenSheet);
    openButton.onClick = [this] { if (onOpenSheet) onOpenSheet(); };
    addAndMakeVisible (openButton);

    styleButton (recordEqButton, {}, "accent");
    recordEqButton.setButtonText (strings::applyRecordEq);
    recordEqButton.setTooltip (strings::applyRecordEqTooltip);
    recordEqButton.onClick = [this]
    {
        applyvalues::recordEq (ctx, ctx.state.currentSystemName, ctx.state.proposedBands);
        update();
    };
    addChildComponent (recordEqButton);
}

void ApplySummaryPanel::update()
{
    const auto& systems = ctx.project.currentVenue().systems;
    const auto name = ctx.state.currentSystemName.isNotEmpty() ? ctx.state.currentSystemName : juce::String (strings::systemsAll);

    juce::String sig = name + "|" + juce::String ((int) ctx.state.proposedBands.size());
    for (const auto& s : systems)
        if (s.name == name)
            sig << "|" << (int) s.delaySet << s.delayMs << (int) s.polaritySet << (int) s.invertPolarity << (int) s.eqSet
                << (int) s.eqBands.size() << (int) s.entered;

    recordEqButton.setVisible (! ctx.state.proposedBands.empty());

    if (sig != signature)
    {
        signature = sig;
        repaint();
    }
}

void ApplySummaryPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 10);
    openButton.setBounds (area.removeFromBottom (28));
    area.removeFromBottom (6);
    recordEqButton.setBounds (area.removeFromBottom (28));
}

void ApplySummaryPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::get (theme::ColourId::raised));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    const auto name = ctx.state.currentSystemName.isNotEmpty() ? ctx.state.currentSystemName : juce::String (strings::systemsAll);
    const eeq::core::SpeakerSystemInfo* system = nullptr;
    for (const auto& s : ctx.project.currentVenue().systems)
        if (s.name == name)
            system = &s;

    auto area = getLocalBounds().reduced (12, 10);
    g.setColour (theme::get (theme::ColourId::textDim));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (strings::applySummaryTitle, area.removeFromTop (16), juce::Justification::centredLeft);
    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawText (name, area.removeFromTop (20), juce::Justification::centredLeft, true);
    area.removeFromTop (6);

    auto row = [&] (const juce::String& title, const juce::String& value, bool decided)
    {
        auto r = area.removeFromTop (22);
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (title, r.removeFromLeft (54), juce::Justification::centredLeft);
        g.setColour (decided ? theme::get (theme::ColourId::text) : theme::get (theme::ColourId::warn));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (value, r, juce::Justification::centredLeft, true);
    };

    const auto undecided = juce::String (strings::applyUndecided);
    row (strings::applyDelay, system != nullptr && system->delaySet ? juce::String (system->delayMs, 2) + " ms" : undecided,
         system != nullptr && system->delaySet);
    row (strings::comparePolarity,
         system != nullptr && system->polaritySet ? (system->invertPolarity ? juce::String (strings::applyPolarityInvert)
                                                                            : juce::String (strings::applyPolarityNormal))
                                                  : undecided,
         system != nullptr && system->polaritySet);
    row ("EQ", system != nullptr && system->eqSet ? (system->eqBands.empty() ? juce::String (strings::applyEqFlat)
                                                                             : juce::String ((int) system->eqBands.size()) + strings::applyEqBandsSuffix)
                                                  : undecided,
         system != nullptr && system->eqSet);
    row (strings::applyEnteredShort, system != nullptr && system->entered ? juce::String (strings::applyEnteredDone)
                                                                          : juce::String (strings::applyEnteredNot),
         system != nullptr && system->entered);
}

//==============================================================================
class ApplySheetPage::SystemCard : public juce::Component
{
public:
    SystemCard (UiContext& c, size_t idx, std::function<void()> changed)
        : ctx (c), index (idx), onChanged (std::move (changed))
    {
        auto& system = sys();

        styleTextEditor (delayEditor);
        delayEditor.setInputRestrictions (8, "0123456789.");
        delayEditor.setJustification (juce::Justification::centredRight);
        delayEditor.setText (system.delaySet ? juce::String (system.delayMs, 2) : juce::String(), juce::dontSendNotification);
        delayEditor.setTextToShowWhenEmpty (strings::applyUndecided, theme::get (theme::ColourId::warn));
        delayEditor.onReturnKey = [this] { commitDelay(); };
        delayEditor.onFocusLost = [this] { commitDelay(); };
        addAndMakeVisible (delayEditor);

        styleCombo (polarityCombo);
        polarityCombo.addItem (strings::applyUndecided, 1);
        polarityCombo.addItem (strings::applyPolarityNormal, 2);
        polarityCombo.addItem (strings::applyPolarityInvert, 3);
        polarityCombo.setSelectedId (system.polaritySet ? (system.invertPolarity ? 3 : 2) : 1, juce::dontSendNotification);
        polarityCombo.onChange = [this]
        {
            auto& s = sys();
            const auto id = polarityCombo.getSelectedId();
            s.polaritySet = id >= 2;
            s.invertPolarity = id == 3;
            s.entered = false;
            notify();
        };
        addAndMakeVisible (polarityCombo);

        styleToggle (enteredToggle);
        enteredToggle.setButtonText (strings::applyEnteredToggle);
        enteredToggle.setToggleState (system.entered, juce::dontSendNotification);
        enteredToggle.onClick = [this]
        {
            sys().entered = enteredToggle.getToggleState();
            notify();
        };
        addAndMakeVisible (enteredToggle);

        styleButton (recordEqButton, {}, "accent");
        recordEqButton.setButtonText (strings::applyRecordEq);
        recordEqButton.setTooltip (strings::applyRecordEqTooltip);
        recordEqButton.setEnabled (! ctx.state.proposedBands.empty());
        recordEqButton.onClick = [this]
        {
            applyvalues::recordEq (ctx, sys().name, ctx.state.proposedBands);
            notify();
        };
        addAndMakeVisible (recordEqButton);

        styleButton (selectButton);
        selectButton.setButtonText (strings::applyMeasureThis);
        selectButton.setTooltip (strings::applyMeasureThisTooltip);
        selectButton.onClick = [this]
        {
            ctx.state.currentSystemName = sys().name;
            notify();
        };
        addAndMakeVisible (selectButton);

        styleTextEditor (noteEditor);
        noteEditor.setText (system.tuningNote, juce::dontSendNotification);
        noteEditor.setTextToShowWhenEmpty (strings::applyNotePlaceholder, theme::get (theme::ColourId::textDim));
        noteEditor.onFocusLost = [this] { sys().tuningNote = noteEditor.getText(); };
        noteEditor.onReturnKey = [this] { sys().tuningNote = noteEditor.getText(); };
        addAndMakeVisible (noteEditor);
    }

    int preferredHeight() const
    {
        const auto bands = sys().eqSet ? juce::jmax (1, (int) sys().eqBands.size()) : 1;
        return 136 + bands * 20;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 12);
        auto header = area.removeFromTop (28);
        selectButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 26));
        header.removeFromRight (8);
        enteredToggle.setBounds (header.removeFromRight (150));
        area.removeFromTop (10);

        auto left = area.removeFromLeft (juce::jmin (300, area.getWidth() / 3));
        area.removeFromLeft (20);

        auto delayRow = left.removeFromTop (30);
        delayRow.removeFromLeft (70);
        delayEditor.setBounds (delayRow.removeFromLeft (110));
        left.removeFromTop (8);
        auto polarityRow = left.removeFromTop (30);
        polarityRow.removeFromLeft (70);
        polarityCombo.setBounds (polarityRow.removeFromLeft (140));
        left.removeFromTop (8);
        noteEditor.setBounds (left.removeFromTop (28));

        eqArea = area;
        recordEqButton.setBounds (area.removeFromTop (28).removeFromRight (150));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& system = sys();
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto current = ctx.state.currentSystemName == system.name;

        g.setColour (theme::get (theme::ColourId::raised));
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (current ? theme::get (theme::ColourId::accent).withAlpha (0.6f) : theme::get (theme::ColourId::controlOutline));
        g.drawRoundedRectangle (bounds, 10.0f, current ? 1.5f : 1.0f);

        auto area = getLocalBounds().reduced (16, 12);
        auto header = area.removeFromTop (28);

        g.setColour (system.colour);
        g.fillEllipse (header.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat());
        header.removeFromLeft (8);
        g.setColour (theme::get (theme::ColourId::text));
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        const auto nameWidth = juce::jmin (header.getWidth() - 290, 260);
        g.drawText (system.name, header.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (strings::systemsChannelLabel + " " + juce::String (system.outputChannel + 1)
                        + (current ? juce::String (u8"  ·  ") + strings::applyMeasuringNow : juce::String()),
                    header, juce::Justification::centredLeft, true);

        area.removeFromTop (10);
        auto left = area.removeFromLeft (juce::jmin (300, area.getWidth() / 3));

        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.setColour (theme::get (theme::ColourId::textDim));
        g.drawText (strings::applyDelay, left.removeFromTop (30).removeFromLeft (66), juce::Justification::centredLeft);
        left.removeFromTop (8);
        g.drawText (strings::comparePolarity, left.removeFromTop (30).removeFromLeft (66), juce::Justification::centredLeft);

        // 「ms」の単位
        g.drawText ("ms", delayEditor.getBounds().translated (delayEditor.getWidth() + 6, 0).withWidth (30),
                    juce::Justification::centredLeft);

        // EQ 表
        auto eq = eqArea;
        auto eqHeader = eq.removeFromTop (28);
        g.setColour (theme::get (theme::ColourId::textDim));
        g.drawText ("EQ", eqHeader, juce::Justification::centredLeft);
        eq.removeFromTop (6);

        if (! system.eqSet)
        {
            g.setColour (theme::get (theme::ColourId::warn));
            g.drawText (strings::applyEqUndecidedHint, eq.removeFromTop (20), juce::Justification::centredLeft);
            return;
        }

        if (system.eqBands.empty())
        {
            g.setColour (theme::get (theme::ColourId::text));
            g.drawText (strings::applyEqFlat, eq.removeFromTop (20), juce::Justification::centredLeft);
            return;
        }

        const int cols[] = { 28, 110, 90, 90, 70 };
        auto headerRow = eq.removeFromTop (18);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const juce::String titles[] = { "#", strings::eqTypeLabel, strings::eqFreqLabel, strings::eqGainLabel, strings::eqQLabel };
        for (int c = 0; c < 5; ++c)
            g.drawText (titles[c], headerRow.removeFromLeft (cols[c]), juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        int n = 1;
        for (const auto& band : system.eqBands)
        {
            auto r = eq.removeFromTop (20);
            g.setColour (band.enabled ? theme::get (theme::ColourId::text) : theme::get (theme::ColourId::textDim));
            const juce::String cells[] = {
                juce::String (n++), applyvalues::bandTypeName (band.type),
                juce::String (band.frequency, band.frequency < 100 ? 1 : 0) + " Hz",
                (band.gainDb >= 0 ? "+" : "") + juce::String (band.gainDb, 1) + " dB", juce::String (band.q, 2)
            };
            for (int c = 0; c < 5; ++c)
                g.drawText (cells[c], r.removeFromLeft (cols[c]), juce::Justification::centredLeft);
        }
    }

private:
    eeq::core::SpeakerSystemInfo& sys() const { return ctx.project.currentVenue().systems[index]; }

    void commitDelay()
    {
        auto& s = sys();
        const auto text = delayEditor.getText().trim();

        if (text.isEmpty())
        {
            if (s.delaySet)
            {
                s.delaySet = false;
                s.entered = false;
                notify();
            }
            return;
        }

        const auto value = text.getDoubleValue();
        if (! s.delaySet || std::abs (value - s.delayMs) > 1.0e-6)
        {
            s.delaySet = true;
            s.delayMs = value;
            s.entered = false;
            notify();
        }
    }

    void notify()
    {
        sys().tuningUpdated = juce::Time::getCurrentTime();
        if (onChanged)
            onChanged();
    }

    UiContext& ctx;
    size_t index;
    std::function<void()> onChanged;
    juce::TextEditor delayEditor, noteEditor;
    juce::ComboBox polarityCombo;
    juce::ToggleButton enteredToggle;
    juce::TextButton recordEqButton, selectButton;
    juce::Rectangle<int> eqArea;
};

//==============================================================================
ApplySheetPage::ApplySheetPage (UiContext& context) : ctx (context)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    styleButton (addSystemButton);
    addSystemButton.setButtonText (strings::applyAddSystem);
    addSystemButton.onClick = [this]
    {
        auto& systems = ctx.project.currentVenue().systems;
        juce::String name;
        for (int n = (int) systems.size() + 1;; ++n)
        {
            name = strings::applyDefaultSystemName + juce::String (n);
            bool used = false;
            for (const auto& s : systems)
                used = used || s.name == name;
            if (! used)
                break;
        }

        eeq::core::SpeakerSystemInfo info;
        info.name = name;
        info.outputChannel = juce::jmax (0, ctx.engine.getOutputChannel());
        const juce::Colour palette[] = { theme::get (theme::ColourId::accent), theme::get (theme::ColourId::snapshotPalette1),
                                         theme::get (theme::ColourId::snapshotPalette3), theme::get (theme::ColourId::snapshotPalette4),
                                         theme::get (theme::ColourId::snapshotPalette2), theme::get (theme::ColourId::snapshotPalette6) };
        info.colour = palette[systems.size() % 6];
        systems.push_back (info);
        ctx.state.snapshotVersion.fetch_add (1);
        rebuildCards();
    };
    addAndMakeVisible (addSystemButton);

    styleButton (copyButton);
    copyButton.setButtonText (strings::applyCopy);
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (applyvalues::sheetAsText (ctx)); };
    addAndMakeVisible (copyButton);

    styleButton (exportButton);
    exportButton.setButtonText (strings::applyExport);
    exportButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (strings::applyExport,
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile (ctx.project.currentVenue().name.isNotEmpty() ? ctx.project.currentVenue().name + ".txt"
                                                                            : juce::String ("settings.txt")),
            "*.txt");
        juce::Component::SafePointer<ApplySheetPage> safeThis (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis, chooser] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (safeThis != nullptr && file != juce::File())
                                      file.replaceWithText (applyvalues::sheetAsText (safeThis->ctx));
                              });
    };
    addAndMakeVisible (exportButton);

    rebuildCards();
}

ApplySheetPage::~ApplySheetPage() = default;

juce::String ApplySheetPage::dataSignature() const
{
    juce::String sig = juce::String ((int) ctx.project.getCurrentVenueIndex()) + "|" + ctx.state.currentSystemName + "|"
                     + juce::String ((int) ctx.state.proposedBands.size());
    for (const auto& s : ctx.project.currentVenue().systems)
        sig << "|" << s.name << (int) s.delaySet << juce::String (s.delayMs, 3) << (int) s.polaritySet << (int) s.invertPolarity
            << (int) s.eqSet << (int) s.eqBands.size() << (int) s.entered << s.outputChannel;
    return sig;
}

void ApplySheetPage::timerTick()
{
    // 入力中の欄があるときは作り直さない（フォーカスが外れたときに反映される）
    for (auto& card : cards)
        if (card->hasKeyboardFocus (true))
            return;

    if (dataSignature() != lastSignature)
        rebuildCards();
}

void ApplySheetPage::rebuildCards()
{
    lastSignature = dataSignature();
    cards.clear();

    const auto count = ctx.project.currentVenue().systems.size();

    for (size_t i = 0; i < count; ++i)
    {
        auto card = std::make_unique<SystemCard> (ctx, i, [this]
        {
            juce::Component::SafePointer<ApplySheetPage> safeThis (this);
            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->rebuildCards();
            });
        });
        content.addAndMakeVisible (*card);
        cards.push_back (std::move (card));
    }

    resized();
    repaint();
}

void ApplySheetPage::resized()
{
    auto area = getLocalBounds().reduced (24, 18);
    auto header = area.removeFromTop (58);
    auto buttons = header.removeFromRight (440).withSizeKeepingCentre (440, 30);
    exportButton.setBounds (buttons.removeFromRight (140));
    buttons.removeFromRight (8);
    copyButton.setBounds (buttons.removeFromRight (140));
    buttons.removeFromRight (8);
    addSystemButton.setBounds (buttons.removeFromRight (140));
    area.removeFromTop (8);

    viewport.setBounds (area);

    const auto width = area.getWidth() - 12;
    int y = 0;
    for (auto& card : cards)
    {
        const auto h = card->preferredHeight();
        card->setBounds (0, y, width, h);
        y += h + 12;
    }

    content.setSize (width, juce::jmax (y, area.getHeight()));
}

void ApplySheetPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    auto area = getLocalBounds().reduced (24, 18);
    auto header = area.removeFromTop (58);
    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText (strings::applySheetTitle, header.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (theme::get (theme::ColourId::textDim));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (strings::applySheetLead, header, juce::Justification::centredLeft, true);

    if (cards.empty())
    {
        area.removeFromTop (40);
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawFittedText (strings::applyEmpty, area.removeFromTop (80), juce::Justification::centredTop, 3);
    }
}

} // namespace eeq::ui
