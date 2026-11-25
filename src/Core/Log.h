#pragma once

#include <JuceHeader.h>

// エラーとデバイス情報をローカルのログファイルに保存する（仕様 6.3）。
// ~/Library/Application Support/EasyEQAssistant/eeq.log

namespace eeq::log
{

/** ログに1行追記する（未初期化なら最初の呼び出しでファイルを作る） */
void write (const juce::String& line);

} // namespace eeq::log
