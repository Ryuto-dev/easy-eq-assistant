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

// 上部バー
inline const juce::String deviceLabel { u8"デバイス" };
inline const juce::String sampleRateLabel { u8"サンプルレート" };
inline const juce::String bufferSizeLabel { u8"バッファ" };
inline const juce::String refLabel { u8"Ref" };
inline const juce::String measLabel { u8"Meas" };
inline const juce::String outLabel { u8"出力" };
inline const juce::String channelNone { u8"なし" };
inline const juce::String internalReference { u8"内部参照" };
inline const juce::String notSelected { u8"-" };

// 下部バー
inline const juce::String startNoiseButton { u8"ピンクノイズ開始" };
inline const juce::String stopNoiseButton { u8"停止" };
inline const juce::String outputLevelLabel { u8"出力レベル" };

// ダイアログ・状態表示
inline const juce::String overLimitTitle { u8"出力レベルの確認" };
inline const juce::String overLimitMessage
    { u8"−20 dBFS を超える出力は、聴覚や機器に負担をかける可能性があります。このレベルで使用しますか？" };
inline const juce::String overLimitOk { u8"このレベルで使用" };
inline const juce::String overLimitCancel { u8"−20 dBFS に戻す" };
inline const juce::String deviceErrorTitle { u8"オーディオデバイスのエラー" };
inline const juce::String deviceErrorMessage
    { u8"デバイスでエラーが発生したため、出力を停止しました。デバイスを選び直してください。" };
inline const juce::String noDeviceInfo { u8"デバイス未接続" };
inline const juce::String okButton { u8"OK" };

inline const juce::String phaseMessage
    { u8"フェーズ1：土台 — デバイス選択 / レベルメーター / ピンクノイズ出力が動作中。次は伝達関数測定（フェーズ2）" };
} // namespace eeq::strings
