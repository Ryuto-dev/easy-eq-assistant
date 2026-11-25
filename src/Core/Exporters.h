#pragma once

#include "../DSP/ImpulseResponseBuilder.h"
#include "../DSP/PEQ.h"
#include "../DSP/TransferFunctionAccumulator.h"

#include "Core/JuceModules.h"

namespace eeq::core::exporters
{

// 書き出し（仕様 6.2）。
// 測定データ: CSV と REW等で読めるテキスト（周波数・dB・位相）
// インパルス応答: WAV（32bit float）
// 補正EQ案: CSV / テキスト
// （画面の図のPNGは UI 側で createComponentSnapshot から保存する）

/** 測定データを CSV（freq, magnitudeDb, phaseDeg, coherence）で書き出す */
[[nodiscard]] juce::String writeSnapshotCsv (const eeq::dsp::SpectrumFrame& frame, const juce::File& file);

/** REW 等で読めるテキスト形式（freq・dB・位相）で書き出す */
[[nodiscard]] juce::String writeSnapshotRewText (const eeq::dsp::SpectrumFrame& frame,
                                                 const juce::String& curveName, const juce::File& file);

/** インパルス応答を 32bit float の WAV で書き出す */
[[nodiscard]] juce::String writeImpulseResponseWav (const eeq::dsp::ImpulseResponse& ir, const juce::File& file);

/** 補正EQ案を CSV（type, freq, gain, Q）で書き出す */
[[nodiscard]] juce::String writeEQProposalCsv (const std::vector<eeq::dsp::peq::Band>& bands, const juce::File& file);

/** 補正EQ案を人が読めるテキストで書き出す（卓に手入力するときの控え） */
[[nodiscard]] juce::String writeEQProposalText (const std::vector<eeq::dsp::peq::Band>& bands, const juce::File& file);

/** SPLログ（本番モード）用: ヘッダ付きでCSVを開く。以降の追記は appendSplLogLine で */
[[nodiscard]] bool createSplLogCsv (const juce::File& file);

/** SPLログに1行追記（time, spl, leq1min, leq15min, leqSession, peak） */
[[nodiscard]] bool appendSplLogLine (const juce::File& file, const juce::String& timeText,
                                     double splDb, double leq1min, double leq15min, double leqSession, double peakDb);

} // namespace eeq::core::exporters
