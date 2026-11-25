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
inline const juce::String degreeSymbol { u8"°" };
inline const juce::String gammaSquared { u8"γ²" };

// フェーズ2
inline const juce::String measurementStartButton { u8"測定開始" };
inline const juce::String measurementStopButton { u8"測定停止" };
inline const juce::String resetAveragesButton { u8"平均リセット" };
inline const juce::String graphPlaceholder { u8"測定開始でカーブが表示されます" };
inline const juce::String measurementPanelTitle { u8"測定設定" };
inline const juce::String fftSizeLabel { u8"FFTサイズ" };
inline const juce::String windowLabel { u8"窓関数" };
inline const juce::String averagingModeLabel { u8"平均方式" };
inline const juce::String averagingCountLabel { u8"平均回数" };
inline const juce::String smoothingLabel { u8"スムージング" };
inline const juce::String averagingModeCumulative { u8"累積平均" };
inline const juce::String averagingModeExponential { u8"指数平均" };
inline const juce::String noSmoothingItem { u8"なし" };
inline const juce::String inputDeviceLabel { u8"入力デバイス" };
inline const juce::String outputDeviceLabel { u8"出力デバイス" };
} // namespace eeq::strings
