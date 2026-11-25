#pragma once

#include "Core/JuceModules.h"

#include <atomic>

namespace eeq::ui::theme
{

// ダーク／ライトテーマ（仕様 5.）。現場の暗い環境を最優先しつつ切替可能にする。
// 色だけで状態を伝えない（文字・形状も併用する）方針は 5.5。

enum class Mode { dark, light };

inline std::atomic<Mode> currentMode { Mode::dark };

inline Mode mode() noexcept { return currentMode.load (std::memory_order_relaxed); }
inline void setMode (Mode m) noexcept { currentMode.store (m, std::memory_order_relaxed); }
inline void toggle() noexcept { setMode (mode() == Mode::dark ? Mode::light : Mode::dark); }

enum class ColourId
{
    windowBackground,
    panelBackground,
    graphBackground,
    grid,
    gridMajor,
    gridText,
    text,
    textDim,
    accent,          // 測定カーブなど主役の色
    accent2,         // 位相など2番手
    snapshotPalette1, // スナップショットの既定パレット
    snapshotPalette2,
    snapshotPalette3,
    snapshotPalette4,
    snapshotPalette5,
    snapshotPalette6,
    ok,
    warn,
    danger,
    control,         // コンボ/スライダー等の背景
    controlOutline,
    buttonOn,
    buttonOff
};

inline juce::Colour get (ColourId id) noexcept
{
    if (mode() == Mode::dark)
    {
        switch (id)
        {
            case ColourId::windowBackground: return juce::Colour (0xff141518);
            case ColourId::panelBackground:  return juce::Colour (0xff1a1c20);
            case ColourId::graphBackground:  return juce::Colour (0xff1a1c20);
            case ColourId::grid:             return juce::Colour (0xff24262c);
            case ColourId::gridMajor:        return juce::Colour (0xff2f3239);
            case ColourId::gridText:         return juce::Colour (0xff8a9099);
            case ColourId::text:             return juce::Colour (0xffe4e6ea);
            case ColourId::textDim:          return juce::Colour (0xff9aa0a8);
            case ColourId::accent:           return juce::Colour (0xff3fc9d2);
            case ColourId::accent2:          return juce::Colour (0xffe0a34f);
            case ColourId::snapshotPalette1: return juce::Colour (0xffe0567a);
            case ColourId::snapshotPalette2: return juce::Colour (0xff8f7ff0);
            case ColourId::snapshotPalette3: return juce::Colour (0xff6fd66f);
            case ColourId::snapshotPalette4: return juce::Colour (0xfff0c04a);
            case ColourId::snapshotPalette5: return juce::Colour (0xff5fa8f0);
            case ColourId::snapshotPalette6: return juce::Colour (0xfff08a4a);
            case ColourId::ok:               return juce::Colour (0xff4fae6d);
            case ColourId::warn:             return juce::Colour (0xffd8b13f);
            case ColourId::danger:           return juce::Colour (0xffd85454);
            case ColourId::control:          return juce::Colour (0xff26282e);
            case ColourId::controlOutline:   return juce::Colour (0xff34373f);
            case ColourId::buttonOn:         return juce::Colour (0xff2e5a63);
            case ColourId::buttonOff:        return juce::Colour (0xff33363d);
        }
    }
    else
    {
        switch (id)
        {
            case ColourId::windowBackground: return juce::Colour (0xfff2f3f5);
            case ColourId::panelBackground:  return juce::Colour (0xffffffff);
            case ColourId::graphBackground:  return juce::Colour (0xffffffff);
            case ColourId::grid:             return juce::Colour (0xffe2e4e9);
            case ColourId::gridMajor:        return juce::Colour (0xffc9cdd6);
            case ColourId::gridText:         return juce::Colour (0xff5a616c);
            case ColourId::text:             return juce::Colour (0xff1a1d22);
            case ColourId::textDim:          return juce::Colour (0xff5a616c);
            case ColourId::accent:           return juce::Colour (0xff0e7c86);
            case ColourId::accent2:          return juce::Colour (0xffa3611a);
            case ColourId::snapshotPalette1: return juce::Colour (0xffc22e50);
            case ColourId::snapshotPalette2: return juce::Colour (0xff5b48c9);
            case ColourId::snapshotPalette3: return juce::Colour (0xff2f8f2f);
            case ColourId::snapshotPalette4: return juce::Colour (0xffa67c00);
            case ColourId::snapshotPalette5: return juce::Colour (0xff2f6fb8);
            case ColourId::snapshotPalette6: return juce::Colour (0xffc25a1a);
            case ColourId::ok:               return juce::Colour (0xff2f7a4b);
            case ColourId::warn:             return juce::Colour (0xff9a7a10);
            case ColourId::danger:           return juce::Colour (0xffb02a2a);
            case ColourId::control:          return juce::Colour (0xffeceef2);
            case ColourId::controlOutline:   return juce::Colour (0xffc9cdd6);
            case ColourId::buttonOn:         return juce::Colour (0xffcfe6e9);
            case ColourId::buttonOff:        return juce::Colour (0xffe2e4e9);
        }
    }

    return juce::Colour (0xff808080);
}

} // namespace eeq::ui::theme
