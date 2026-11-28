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
    buttonOff,
    // 2026-11 リデザインで追加
    sidebarBackground, // 左の作業ナビ・上下のバー
    raised,            // カード（パネルの上に載る面）
    hover,             // 行・ボタンのホバー
    onAccent,          // アクセント色で塗った面の上の文字
    accentSoft         // アクセントの薄い面（選択中のナビなど）
};

inline juce::Colour get (ColourId id) noexcept
{
    // 落ち着いたグラファイト＋シアン 1 色（2026-11 リデザイン）。状態色（ok/warn/danger）は彩度を抑え、
    // 主役のライブ曲線だけがはっきり目立つようにする。トレースの既定色は色覚多様性に配慮して
    // 明度と色相を離している（線種でも区別する: 仕様 5.5）
    if (mode() == Mode::dark)
    {
        switch (id)
        {
            case ColourId::windowBackground:  return juce::Colour (0xff0d1014);
            case ColourId::panelBackground:   return juce::Colour (0xff12161b);
            case ColourId::graphBackground:   return juce::Colour (0xff0f1317);
            case ColourId::grid:              return juce::Colour (0xff1a2027);
            case ColourId::gridMajor:         return juce::Colour (0xff27303a);
            case ColourId::gridText:          return juce::Colour (0xff7a8390);
            case ColourId::text:              return juce::Colour (0xffe6e9ee);
            case ColourId::textDim:           return juce::Colour (0xff8b93a0);
            case ColourId::accent:            return juce::Colour (0xff5ee0d2);
            case ColourId::accent2:           return juce::Colour (0xfff5b04c);
            case ColourId::snapshotPalette1:  return juce::Colour (0xffc084fc);
            case ColourId::snapshotPalette2:  return juce::Colour (0xfff472b6);
            case ColourId::snapshotPalette3:  return juce::Colour (0xff60a5fa);
            case ColourId::snapshotPalette4:  return juce::Colour (0xfffacc15);
            case ColourId::snapshotPalette5:  return juce::Colour (0xff4ade80);
            case ColourId::snapshotPalette6:  return juce::Colour (0xfffb923c);
            case ColourId::ok:                return juce::Colour (0xff4ade80);
            case ColourId::warn:              return juce::Colour (0xfffbbf24);
            case ColourId::danger:            return juce::Colour (0xfff87171);
            case ColourId::control:           return juce::Colour (0xff171c22);
            case ColourId::controlOutline:    return juce::Colour (0xff272e38);
            case ColourId::buttonOn:          return juce::Colour (0xff17323a);
            case ColourId::buttonOff:         return juce::Colour (0xff1a1f26);
            case ColourId::sidebarBackground: return juce::Colour (0xff0a0d10);
            case ColourId::raised:            return juce::Colour (0xff161b21);
            case ColourId::hover:             return juce::Colour (0xff1e242c);
            case ColourId::onAccent:          return juce::Colour (0xff06201d);
            case ColourId::accentSoft:        return juce::Colour (0xff15292c);
        }
    }
    else
    {
        switch (id)
        {
            case ColourId::windowBackground:  return juce::Colour (0xfff3f5f8);
            case ColourId::panelBackground:   return juce::Colour (0xffffffff);
            case ColourId::graphBackground:   return juce::Colour (0xffffffff);
            case ColourId::grid:              return juce::Colour (0xffe8ebf0);
            case ColourId::gridMajor:         return juce::Colour (0xffd2d7df);
            case ColourId::gridText:          return juce::Colour (0xff5b6472);
            case ColourId::text:              return juce::Colour (0xff141920);
            case ColourId::textDim:           return juce::Colour (0xff5b6472);
            case ColourId::accent:            return juce::Colour (0xff0d8f84);
            case ColourId::accent2:           return juce::Colour (0xffb26a10);
            case ColourId::snapshotPalette1:  return juce::Colour (0xff7c3aed);
            case ColourId::snapshotPalette2:  return juce::Colour (0xffdb2777);
            case ColourId::snapshotPalette3:  return juce::Colour (0xff2563eb);
            case ColourId::snapshotPalette4:  return juce::Colour (0xffa16207);
            case ColourId::snapshotPalette5:  return juce::Colour (0xff15803d);
            case ColourId::snapshotPalette6:  return juce::Colour (0xffc2410c);
            case ColourId::ok:                return juce::Colour (0xff15803d);
            case ColourId::warn:              return juce::Colour (0xffa16207);
            case ColourId::danger:            return juce::Colour (0xffc62828);
            case ColourId::control:           return juce::Colour (0xfff5f7fa);
            case ColourId::controlOutline:    return juce::Colour (0xffd2d7df);
            case ColourId::buttonOn:          return juce::Colour (0xffd8f1ee);
            case ColourId::buttonOff:         return juce::Colour (0xffffffff);
            case ColourId::sidebarBackground: return juce::Colour (0xffeaedf2);
            case ColourId::raised:            return juce::Colour (0xfff7f8fa);
            case ColourId::hover:             return juce::Colour (0xffe9edf2);
            case ColourId::onAccent:          return juce::Colour (0xffffffff);
            case ColourId::accentSoft:        return juce::Colour (0xffdcf3f0);
        }
    }

    return juce::Colour (0xff808080);
}

} // namespace eeq::ui::theme
