#pragma once

#include <JuceHeader.h>

#include "../Audio/AudioEngine.h"
#include "LevelMeter.h"

namespace eeq::ui
{

// 上部バー（仕様 5.1）: デバイス選択、サンプルレート、バッファ、
// 入出力チャンネル割当（Ref / Meas / 出力）、レベルメーター。
class TopBar : public juce::Component, private juce::ComboBox::Listener
{
public:
    explicit TopBar (eeq::audio::AudioEngine& engine);

    /** 30Hz程度のタイマーから呼ぶ。メーター更新と、デバイス変化の検出を行う */
    void timerTick();

    /** デバイス適用などで失敗したときの通知先 */
    std::function<void (const juce::String&)> onError;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void comboBoxChanged (juce::ComboBox*) override;

    void rebuildAll();
    void rebuildRateCombo();
    void rebuildBufferCombo();
    void rebuildChannelCombo (juce::ComboBox& combo, const juce::Array<int>& indices, const juce::StringArray& names, int selected);

    eeq::audio::AudioEngine& engine;

    juce::Label deviceTitle, rateTitle, bufferTitle, refTitle, measTitle, outTitle;
    juce::ComboBox deviceCombo, rateCombo, bufferCombo, refCombo, measCombo, outCombo;

    LevelMeter refMeter, measMeter, outMeter;

    uint32_t lastGeneration = 0;
    bool updating = false; // コンボ再構築中のイベントを無視するためのフラグ

    static int comboIdForChannel (int channelIndex) noexcept { return channelIndex < 0 ? 1 : channelIndex + 2; }
    static int channelForComboId (int comboId) noexcept { return comboId <= 1 ? -1 : comboId - 2; }
};

} // namespace eeq::ui
