#include "StatusStrip.h"

#include <algorithm>

#include "Buttons.h"
#include "Strings.h"
#include "Theme.h"

#include "../Core/Acoustics.h"

#include <cmath>
#include <limits>

namespace eeq::ui
{

StatusStrip::StatusStrip (UiContext& context)
    : ctx (context), refMeter (strings::refLabel), measMeter (strings::measLabel)
{
    for (auto* meter : { &refMeter, &measMeter })
    {
        meter->onClick = [this] { ctx.engine.clearClipLatches(); };
        meter->setTooltip (strings::meterTooltip);
        addAndMakeVisible (meter);
    }

    styleButton (liveButton, {}, "accent");
    liveButton.setButtonText (strings::tabLiveMode);
    liveButton.setTooltip (strings::liveModeTooltip);
    liveButton.onClick = [this] { if (onLiveModeRequested) onLiveModeRequested(); };
    addAndMakeVisible (liveButton);
}

juce::Colour StatusStrip::toneColour (Tone tone) const
{
    switch (tone)
    {
        case Tone::ok:     return theme::get (theme::ColourId::ok);
        case Tone::warn:   return theme::get (theme::ColourId::warn);
        case Tone::danger: return theme::get (theme::ColourId::danger);
        case Tone::accent: return theme::get (theme::ColourId::accent);
        case Tone::neutral:
        default:           return theme::get (theme::ColourId::textDim);
    }
}

void StatusStrip::update()
{
    auto& engine = ctx.engine;
    auto& measurement = engine.getMeasurement();

    // メーター
    auto toDb = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); };
    auto applyMeter = [&toDb] (const eeq::audio::MeterState& source, LevelMeter& target)
    {
        target.setLevels (toDb (source.peakLinear.load (std::memory_order_relaxed)),
                          toDb (source.rmsLinear.load (std::memory_order_relaxed)),
                          source.clip.load (std::memory_order_relaxed));
    };
    applyMeter (engine.getRefMeter(), refMeter);
    applyMeter (engine.getMeasMeter(), measMeter);

    std::vector<Pill> next;
    const auto rate = engine.getCurrentSampleRate();
    const auto deviceOpen = rate > 0.0 && engine.getSelectedInputDeviceName().isNotEmpty();

    // 入出力
    {
        Pill p { strings::statusIo, {}, Tone::neutral, Target::setup };
        if (! deviceOpen)
        {
            p.value = strings::noDeviceInfo;
            p.tone = Tone::danger;
        }
        else
        {
            p.value = juce::String (rate / 1000.0, rate == std::floor (rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz · "
                    + juce::String (engine.getCurrentBufferSize());
        }
        p.priority = 3;
        next.push_back (p);
    }

    // クロック（入出力が別デバイスなら、ドリフトを補正しているか）
    {
        Pill p { strings::statusClock, {}, Tone::ok, Target::setup };
        const auto sameDevice = engine.getSelectedInputDeviceName() == engine.getSelectedOutputDeviceName();
        const auto compensating = measurement.getCompensatedDriftPpm();

        if (sameDevice)
            p.value = strings::statusClockSame;
        else if (compensating != 0.0f)
        {
            p.value = strings::statusClockCompensating + (compensating > 0 ? "+" : "")
                    + juce::String (juce::roundToInt (compensating)) + " ppm";
            p.tone = Tone::accent;
        }
        else
        {
            p.value = strings::statusClockSeparate;
            p.tone = Tone::neutral;
        }
        p.priority = 5;
        next.push_back (p);
    }

    // 出力
    {
        Pill p { strings::statusOutput, {}, Tone::neutral, Target::measure };
        if (engine.isNoiseAudible())
        {
            p.value = juce::String (u8"● ON  ") + juce::String (engine.getNoiseLevelDb(), 1) + " dBFS";
            p.tone = Tone::danger;
        }
        else
        {
            p.value = "OFF";
        }
        p.priority = 0;
        next.push_back (p);
    }

    // 遅延
    {
        Pill p { strings::delayLabel, {}, Tone::neutral, Target::delay };
        const auto samples = ctx.state.appliedDelaySamples.load();

        if (std::abs (samples) < 0.5)
        {
            p.value = strings::statusDelayNone;
            p.tone = measurement.isMeasuring() ? Tone::warn : Tone::neutral;
        }
        else
        {
            p.value = juce::String (eeq::acoustics::delayMsForSamples (samples, rate > 0.0 ? rate : 48000.0), 2) + " ms";
            p.tone = Tone::ok;
        }
        p.priority = 1;
        next.push_back (p);
    }

    // 平均
    {
        Pill p { strings::statusAverages, {}, Tone::neutral, Target::measure };
        const auto averages = measurement.getAverages();
        const auto capacity = measurement.getAverageCapacity();

        if (! measurement.isMeasuring())
            p.value = strings::statusStopped;
        else
        {
            p.value = juce::String (averages)
                    + (capacity < std::numeric_limits<int>::max() ? " / " + juce::String (capacity) : juce::String());
            p.tone = averages >= 2 ? Tone::ok : Tone::warn;
        }
        p.priority = 2;
        next.push_back (p);
    }

    // ループバック補正
    {
        Pill p { strings::statusCorrection, {}, Tone::neutral, Target::setup };
        const auto on = ctx.correction.isActive() && ctx.state.loopbackEnabled.load();
        p.value = on ? strings::statusOn : strings::statusNone;
        p.tone = on ? Tone::ok : Tone::neutral;
        p.priority = 6;
        next.push_back (p);
    }

    // マイク補正（読み込んでいるファイルを常に見せる: ロードマップ B-5）
    {
        Pill p { strings::statusMic, {}, Tone::neutral, Target::setup };
        if (ctx.state.micCalibration != nullptr)
        {
            p.value = ctx.state.micCalibration->name.upToLastOccurrenceOf (".", false, false);
            if (p.value.length() > 14)
                p.value = p.value.substring (0, 13) + juce::String (u8"…");
            p.tone = Tone::ok;
        }
        else
        {
            p.value = strings::statusNone;
        }
        p.priority = 8;
        next.push_back (p);
    }

    // SPL 校正
    {
        Pill p { strings::statusCalibration, {}, Tone::neutral, Target::setup };
        const auto calibrated = std::abs (ctx.settings.getDoubleValue ("splCalibrationOffsetDb", 0.0)) > 1.0e-6;
        p.value = calibrated ? strings::statusDone : strings::statusNotYet;
        p.tone = calibrated ? Tone::ok : Tone::neutral;
        p.priority = 7;
        next.push_back (p);
    }

    // クリップ（起きたときだけ出す）
    if (engine.getRefMeter().clip.load() || engine.getMeasMeter().clip.load())
    {
        Pill p { strings::statusInput, strings::inputMeterClipLabel, Tone::danger, Target::setup };
        p.priority = -1;
        next.insert (next.begin(), p);
    }

    auto& venue = ctx.project.currentVenue();
    const auto newVenue = venue.name;
    const auto newSystem = ctx.state.currentSystemName.isNotEmpty() ? ctx.state.currentSystemName
                                                                      : juce::String (strings::systemsAll);

    juce::String signature = newVenue + "|" + newSystem;
    for (const auto& p : next)
        signature << "|" << p.value << (int) p.tone;

    if (signature != lastSignature)
    {
        lastSignature = signature;
        pills = std::move (next);
        venueName = newVenue;
        systemName = newSystem;
        layoutPills();
        repaint();
    }
}

void StatusStrip::resized()
{
    auto area = getLocalBounds().reduced (14, 0);

    brandArea = area.removeFromLeft (92);
    liveButton.setBounds (area.removeFromRight (84).withSizeKeepingCentre (84, 30));
    area.removeFromRight (12);

    const auto meterWidth = getWidth() >= 1400 ? 120 : 96;
    measMeter.setBounds (area.removeFromRight (meterWidth).withSizeKeepingCentre (meterWidth, 26));
    area.removeFromRight (8);
    refMeter.setBounds (area.removeFromRight (meterWidth).withSizeKeepingCentre (meterWidth, 26));
    area.removeFromRight (14);

    contextArea = area.removeFromLeft (juce::jlimit (140, 260, area.getWidth() / 4));
    area.removeFromLeft (10);
    pillArea = area;
    layoutPills();
}

void StatusStrip::layoutPills()
{
    // 幅に収まるまで優先度の低い項目から隠す
    const juce::Font titleFont (juce::FontOptions (10.5f));
    const juce::Font valueFont (juce::FontOptions (13.0f));

    auto widthOf = [&] (const Pill& p)
    {
        return (int) std::ceil (juce::jmax (juce::GlyphArrangement::getStringWidth (titleFont, p.title),
                                            juce::GlyphArrangement::getStringWidth (valueFont, p.value))) + 20;
    };

    std::vector<int> visible;
    for (size_t i = 0; i < pills.size(); ++i)
        visible.push_back ((int) i);

    auto totalWidth = [&]
    {
        int w = 0;
        for (auto i : visible)
            w += widthOf (pills[(size_t) i]) + 6;
        return w;
    };

    while (! visible.empty() && totalWidth() > pillArea.getWidth())
    {
        auto worst = std::max_element (visible.begin(), visible.end(), [&] (int a, int b)
                                       { return pills[(size_t) a].priority < pills[(size_t) b].priority; });
        visible.erase (worst);
    }

    for (auto& p : pills)
        p.bounds = {};

    auto x = pillArea.getX();
    for (auto i : visible)
    {
        auto& p = pills[(size_t) i];
        const auto w = widthOf (p);
        p.bounds = { x, pillArea.getCentreY() - 18, w, 36 };
        x += w + 6;
    }
}

void StatusStrip::paint (juce::Graphics& g)
{
    g.fillAll (theme::get (theme::ColourId::sidebarBackground));
    g.setColour (theme::get (theme::ColourId::controlOutline));
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // ロゴ
    g.setColour (theme::get (theme::ColourId::accent));
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
    g.drawText ("EASY EQ", brandArea, juce::Justification::centredLeft);

    // 会場 › 系統
    {
        auto r = contextArea;
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (venueName.isNotEmpty() ? venueName : juce::String (strings::statusNoVenue),
                    r.removeFromTop (getHeight() / 2).withTrimmedTop (6), juce::Justification::bottomLeft, true);
        g.setColour (theme::get (theme::ColourId::text));
        g.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
        g.drawText (strings::statusSystemPrefix + systemName, r.withTrimmedBottom (6), juce::Justification::topLeft, true);
    }

    for (const auto& p : pills)
    {
        if (p.bounds.isEmpty())
            continue;

        const auto colour = toneColour (p.tone);
        const auto r = p.bounds.toFloat();

        if (p.tone == Tone::danger || p.tone == Tone::warn)
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (colour.withAlpha (0.5f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }

        auto inner = p.bounds.reduced (10, 3);
        g.setColour (theme::get (theme::ColourId::textDim));
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (p.title, inner.removeFromTop (14), juce::Justification::bottomLeft);
        g.setColour (p.tone == Tone::neutral ? theme::get (theme::ColourId::text) : colour);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (p.value, inner, juce::Justification::centredLeft);
    }
}

void StatusStrip::mouseMove (const juce::MouseEvent& event)
{
    bool overPill = false;
    for (const auto& p : pills)
        if (p.bounds.contains (event.getPosition()) && p.target != Target::none)
            overPill = true;

    setMouseCursor (overPill ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void StatusStrip::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasDraggedSinceMouseDown())
        return;

    for (const auto& p : pills)
        if (p.bounds.contains (event.getPosition()) && p.target != Target::none && onNavigate)
        {
            onNavigate (p.target);
            return;
        }
}

} // namespace eeq::ui
