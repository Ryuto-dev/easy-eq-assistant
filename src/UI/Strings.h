#pragma once

#include <JuceHeader.h>

namespace eeq::strings
{
// すべての UI 文字列はこのファイル（またはこの単位）に集める。
// 日本語のみで開発するが、将来の英語翻訳に備えて一元管理する（検討事項の決定事項）。

inline const juce::String appName { "Easy EQ assistant" };

inline const juce::String phaseMessage
    { "フェーズ1：土台 — 次はデバイス選択とレベルメーター" };
} // namespace eeq::strings
