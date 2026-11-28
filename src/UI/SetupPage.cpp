#include "SetupPage.h"

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include <cmath>

namespace eeq::ui
{

SetupPage::SetupPage (UiContext& context)
    : ctx (context), devicePanel (context.engine)
{
    devicePanel.onError = [this] (const juce::String& message) { if (onError) onError (message); };
    addAndMakeVisible (devicePanel);

    styleButton (startButton, {}, "primary");
    startButton.setButtonText (strings::setupGoMeasure);
    startButton.onClick = [this] { if (onNavigate) onNavigate (Destination::measure); };
    addAndMakeVisible (startButton);

    evaluate();
}

void SetupPage::evaluate()
{
    auto& engine = ctx.engine;
    std::vector<Check> next;

    const auto rate = engine.getCurrentSampleRate();
    const auto inputName = engine.getSelectedInputDeviceName();
    const auto outputName = engine.getSelectedOutputDeviceName();
    const auto deviceOpen = rate > 0.0 && inputName.isNotEmpty();

    // 1. 機器
    {
        Check c { strings::checkDeviceTitle, {}, State::ok, true, Destination::measure, {} };
        if (! deviceOpen)
        {
            c.state = State::bad;
            c.detail = strings::checkDeviceMissing;
        }
        else
        {
            c.detail = inputName + juce::String (u8" → ") + outputName + "  ·  " + juce::String (juce::roundToInt (rate)) + " Hz";
        }
        next.push_back (c);
    }

    // 2. マイクの入力レベル（鳴らしている間のピーク。目標 −30〜−6 dBFS）
    {
        Check c { strings::checkInputTitle, {}, State::info, true, Destination::gainAssist, strings::checkOpenGainAssist };
        const auto peakDb = juce::Decibels::gainToDecibels (engine.getMeasMeter().peakLinear.load(), -100.0f);

        if (engine.getMeasMeter().clip.load())
        {
            c.state = State::bad;
            c.detail = strings::checkInputClip;
        }
        else if (! engine.isNoiseAudible())
        {
            c.state = State::info;
            c.detail = strings::checkInputPlayNoise;
        }
        else if (peakDb > -6.0f)
        {
            c.state = State::warn;
            c.detail = strings::checkInputHot + juce::String (peakDb, 1) + " dBFS";
        }
        else if (peakDb < -40.0f)
        {
            c.state = State::warn;
            c.detail = strings::checkInputLow + juce::String (peakDb, 1) + " dBFS";
        }
        else
        {
            c.state = State::ok;
            c.detail = strings::checkInputOk + juce::String (peakDb, 1) + " dBFS";
        }
        next.push_back (c);
    }

    // 3. Ref
    {
        Check c { strings::checkRefTitle, {}, State::ok, true, Destination::measure, {} };
        if (engine.isRefInternal())
            c.detail = strings::checkRefInternal;
        else if (engine.getRefChannel() < 0)
        {
            c.state = State::bad;
            c.detail = strings::checkRefMissing;
        }
        else
        {
            const auto refPeak = juce::Decibels::gainToDecibels (engine.getRefMeter().peakLinear.load(), -100.0f);
            c.detail = strings::checkRefExternal + juce::String (engine.getRefChannel() + 1);
            if (engine.isNoiseAudible() && refPeak < -60.0f)
            {
                c.state = State::warn;
                c.detail << strings::checkRefSilent;
            }
        }
        next.push_back (c);
    }

    // 4. クロック
    {
        Check c { strings::checkClockTitle, {}, State::ok, true, Destination::measure, {} };
        if (inputName == outputName)
            c.detail = strings::checkClockSame;
        else
        {
            const auto ppm = engine.getMeasurement().getCompensatedDriftPpm();
            c.detail = ppm != 0.0f ? strings::checkClockCompensating + juce::String (juce::roundToInt (ppm)) + " ppm"
                                   : juce::String (strings::checkClockSeparate);
        }
        next.push_back (c);
    }

    // 5. ループバック補正（任意）
    {
        Check c { strings::checkLoopbackTitle, {}, State::optional, false, Destination::loopback, strings::checkOpenLoopback };
        if (ctx.correction.isActive() && ctx.state.loopbackEnabled.load())
        {
            c.state = State::ok;
            c.detail = strings::checkLoopbackOn;
        }
        else
        {
            c.detail = strings::checkLoopbackOff;
        }
        next.push_back (c);
    }

    // 6. SPL 校正（任意）
    {
        Check c { strings::checkSplTitle, {}, State::optional, false, Destination::splCalibration, strings::checkOpenSpl };
        const auto offset = ctx.settings.getDoubleValue ("splCalibrationOffsetDb", 0.0);
        if (std::abs (offset) > 1.0e-6)
        {
            c.state = State::ok;
            c.detail = strings::checkSplDone + juce::String (offset, 1) + " dB";
        }
        else
        {
            c.detail = strings::checkSplNotYet;
        }
        next.push_back (c);
    }

    bool allRequiredOk = true, anyProblem = false;
    juce::String sig;
    for (const auto& c : next)
    {
        if (c.required && c.state != State::ok)
            allRequiredOk = false;
        if (c.required && (c.state == State::bad || c.state == State::warn))
            anyProblem = true;
        sig << c.detail << (int) c.state << "|";
    }

    // 入力レベルが「ノイズを出して確認」のままなら完了扱いにしない（一度は確かめてほしい）。
    // ただし一度 OK になったら、ノイズを止めても完了のままにする
    if (allRequiredOk)
        inputVerified = true;
    if (! deviceOpen)
        inputVerified = false;

    ready = inputVerified && deviceOpen && ! anyProblem;
    attention = anyProblem;

    if (sig != signature)
    {
        signature = sig;
        const bool countChanged = checks.size() != next.size();
        checks = std::move (next);

        if (countChanged || actionButtons.size() != (int) checks.size())
        {
            actionButtons.clear();
            for (size_t i = 0; i < checks.size(); ++i)
            {
                auto* button = actionButtons.add (new juce::TextButton());
                styleButton (*button);
                const auto destination = checks[i].destination;
                button->onClick = [this, destination] { if (onNavigate) onNavigate (destination); };
                addAndMakeVisible (button);
            }
        }

        for (size_t i = 0; i < checks.size(); ++i)
        {
            actionButtons[(int) i]->setButtonText (checks[i].actionText);
            actionButtons[(int) i]->setVisible (checks[i].actionText.isNotEmpty());
        }

        resized();
        repaint();
    }
}

void SetupPage::timerTick()
{
    devicePanel.timerTick();
    evaluate();
}

void SetupPage::resized()
{
    auto area = getLocalBounds().reduced (24, 18);
    area.removeFromTop (58); // 見出し
    devicePanel.setBounds (area.removeFromTop (TopBar::preferredHeight));
    area.removeFromTop (18);

    auto footer = area.removeFromBottom (40);
    startButton.setBounds (footer.removeFromRight (220).withSizeKeepingCentre (220, 36));

    checklistArea = area.withTrimmedBottom (10);
    auto rows = checklistArea.reduced (20, 0).withTrimmedTop (40);
    const auto rowHeight = juce::jlimit (36, 52, rows.getHeight() / juce::jmax (1, (int) checks.size()));

    for (int i = 0; i < actionButtons.size(); ++i)
    {
        auto r = rows.removeFromTop (rowHeight);
        actionButtons[i]->setBounds (r.removeFromRight (170).withSizeKeepingCentre (170, 28));
    }
}

void SetupPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::windowBackground));

    auto area = getLocalBounds().reduced (24, 18);
    auto header = area.removeFromTop (58);
    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText (strings::setupTitle, header.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (theme::get (theme::ColourId::textDim));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (strings::setupLead, header, juce::Justification::centredLeft, true);

    // 確認リストのカード
    const auto card = checklistArea.toFloat().reduced (0.5f);
    g.setColour (theme::get (theme::ColourId::raised));
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.drawRoundedRectangle (card, 10.0f, 1.0f);

    auto rows = checklistArea.reduced (20, 0);
    g.setColour (theme::get (theme::ColourId::text));
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText (strings::setupChecklistTitle, rows.removeFromTop (40), juce::Justification::centredLeft);

    const auto rowHeight = juce::jlimit (36, 52, rows.getHeight() / juce::jmax (1, (int) checks.size()));

    for (size_t i = 0; i < checks.size(); ++i)
    {
        const auto& c = checks[i];
        auto r = rows.removeFromTop (rowHeight);

        if (i > 0)
        {
            g.setColour (theme::get (theme::ColourId::controlOutline).withAlpha (0.6f));
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        }

        juce::Colour colour;
        juce::String mark;
        switch (c.state)
        {
            case State::ok:       colour = theme::get (theme::ColourId::ok);     mark = "OK"; break;
            case State::warn:     colour = theme::get (theme::ColourId::warn);   mark = strings::checkMarkWarn; break;
            case State::bad:      colour = theme::get (theme::ColourId::danger); mark = strings::checkMarkBad; break;
            case State::optional: colour = theme::get (theme::ColourId::textDim); mark = strings::checkMarkOptional; break;
            case State::info:
            default:              colour = theme::get (theme::ColourId::accent); mark = strings::checkMarkTodo; break;
        }

        auto badge = r.removeFromLeft (64).withSizeKeepingCentre (56, 22).toFloat();
        g.setColour (colour.withAlpha (0.14f));
        g.fillRoundedRectangle (badge, 11.0f);
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText (mark, badge, juce::Justification::centred);

        r.removeFromLeft (8);
        r.removeFromRight (180);
        g.setColour (theme::get (theme::ColourId::text));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText (c.title, r.removeFromLeft (170), juce::Justification::centredLeft, true);
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (c.detail, r, juce::Justification::centredLeft, true);
    }
}

} // namespace eeq::ui
