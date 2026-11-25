#pragma once

#include <JuceHeader.h>

namespace eeq::strings
{
// すべての UI 文字列はこのファイル（またはこの単位）に集める。
// 日本語のみで開発するが、将来の英語翻訳に備えて一元管理する（検討事項の決定事項）。
//
// 注意: JUCE の String(const char*) は ASCII 前提で、127 を超えるバイトを
// 1バイト=1文字として扱う（文字化けの原因）。非 ASCII を含むリテラルには
// 必ず u8 プレフィックスを付け、C++20 の char8_t 版コンストラクタを使う。

inline const juce::String appName { u8"Easy EQ assistant" };

inline const juce::String phaseMessage
    { u8"フェーズ1：土台 — 次はデバイス選択とレベルメーター" };
} // namespace eeq::strings
