#pragma once

#include "Core/JuceModules.h"

namespace eeq::audio
{

// MacBook の内蔵マイクの回避策（macOS）。
//
// 内蔵マイクは、内蔵スピーカーのデバイスがどこかのプロセスで動いている間しか音を出さない
// （止まっているとコールバックは来るのに全サンプルが 0 になる）。この Mac / macOS 27 で確認:
// JUCE を使わない ffmpeg の録音でも同じで、別プロセスが内蔵スピーカーへ無音を再生すると録れる。
// そのため「入力 = 内蔵マイク、出力 = 内蔵スピーカー以外（IF・仮想デバイス・ヘッドフォン）」の
// 構成では、内蔵スピーカーへ無音を流し続けて内蔵マイクを生かしておく。
// 内蔵スピーカーの音量が 0 / ミュートのときは、これを使っても内蔵マイクは無音のまま（ユーザー報告）。
//
// メッセージスレッドから呼ぶこと。CoreAudio の IOProc はゼロを書くだけで、アプリの音声経路とは独立。
class BuiltInSpeakerKeepAlive
{
public:
    BuiltInSpeakerKeepAlive() = default;
    ~BuiltInSpeakerKeepAlive();

    /** 選択中の入力/出力デバイス名から必要性を判定し、無音再生を開始/停止する。
        戻り値: 無音再生を動かしているか */
    bool update (const juce::String& inputDeviceName, const juce::String& outputDeviceName);

    /** 入力が内蔵マイクか（最後の update の判定） */
    bool inputIsBuiltInMic() const noexcept { return builtInInput; }

    bool isActive() const noexcept;

    void stop();

private:
    bool builtInInput = false;

   #if JUCE_MAC
    unsigned int runningDevice = 0;
    void* procId = nullptr;
   #endif

    JUCE_DECLARE_NON_COPYABLE (BuiltInSpeakerKeepAlive)
};

} // namespace eeq::audio
