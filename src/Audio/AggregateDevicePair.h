#pragma once

#include "Core/JuceModules.h"

#include <list>

namespace eeq::audio
{

// 入力と出力が別デバイスのときに使う、このプロセス専用の CoreAudio 集約デバイス（macOS）。
//
// JUCE は別デバイスの入出力を AudioIODeviceCombiner で FIFO 越しにつなぐだけで、クロックの差を
// 補正しない。MacBook のスピーカー → BMG-22M の実測で遅延が約 4.3 サンプル/秒（≈90 ppm）ずつ
// 動き続け、FIFO の破綻で数十サンプル跳ぶため、内部参照の伝達関数は 1 kHz 以上のコヒーレンスが
// ほぼ 0 になっていた。集約デバイスにすると入出力が 1 つの IO サイクルで動き、FIFO の破綻
// （数十サンプルの跳び）がなくなる。OS のドリフト補正も有効にするが、この組では効かず
// 約 83 ppm のずれが残ったため、残りは MeasurementEngine の ClockDriftCompensator が補正する。
//
// サブデバイスの順番は [入力デバイス, 出力デバイス]。集約デバイスの入力chは入力デバイスの分が
// 先頭 (0..inputChannels-1)、出力chは入力デバイスの出力の後ろ (outputOffset..) に並ぶ。
// クロックの主は入力デバイス（マイク側）、出力デバイスをドリフト補正する。
//
// 集約デバイスはプライベート（このプロセスからしか見えず、終了時に OS が消す）。
// メッセージスレッドからだけ使うこと。
class AggregateDevicePair
{
public:
    struct Pair
    {
        juce::String inputName, outputName; // 元の（論理）デバイス名
        juce::String aggregateName;         // JUCE に渡す名前
        int inputChannels = 0;              // 入力デバイスの入力ch数（集約の入力 0..）
        int outputOffset = 0;               // 集約の出力で、出力デバイスの先頭ch
        int outputChannels = 0;             // 出力デバイスの出力ch数
    };

    AggregateDevicePair() = default;
    ~AggregateDevicePair();

    /** このプラットフォームで集約デバイスを使えるか（macOS のみ） */
    static constexpr bool isSupported() noexcept
    {
       #if JUCE_MAC
        return true;
       #else
        return false;
       #endif
    }

    /** 入力/出力の組の集約デバイスを用意する（同じ組なら作り直さない）。
        失敗したら nullptr（呼び出し側は従来どおり別デバイスで開く）。 */
    const Pair* prepare (const juce::String& inputName, const juce::String& outputName, juce::String& errorOut);

    /** 名前がこのクラスの作った集約デバイスか */
    static bool isAggregateName (const juce::String& name);

    /** aggregateName の組を返す（無ければ nullptr） */
    const Pair* find (const juce::String& aggregateName) const;

    /** keepAggregateName 以外の集約デバイスを破棄する（JUCE がもう開いていないもの） */
    void destroyAllExcept (const juce::String& keepAggregateName);

private:
    struct Created
    {
        Pair pair;
        unsigned int objectId = 0;
    };

    std::list<Created> created; // 要素のアドレスが変わらない（prepare/find が返すポインタを保つ）
    int counter = 0;

    JUCE_DECLARE_NON_COPYABLE (AggregateDevicePair)
};

} // namespace eeq::audio
