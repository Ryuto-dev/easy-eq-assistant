#pragma once

#include "Core/JuceModules.h"

#include "TopBar.h"
#include "UiContext.h"

#include <functional>
#include <vector>

namespace eeq::ui
{

// 「1 準備」画面（2026-11 リデザイン / ロードマップ 3.5 プリフライトチェック）。
// 上: 機器とチャンネル割当、レベルメーター（旧・上部バー）
// 下: 測る前の確認リスト。各項目は合否（色の点と文字）と、直す場所へ移動するボタンを持つ。
//   必須: 機器 / 入力レベル / Ref / クロック     任意: ループバック補正 / SPL 校正
class SetupPage : public juce::Component
{
public:
    enum class Destination { loopback, gainAssist, splCalibration, measure };

    explicit SetupPage (UiContext& context);

    void timerTick();

    /** 必須の確認項目がすべて OK か（左のナビの完了マーク） */
    bool isReady() const noexcept { return ready; }
    bool needsAttention() const noexcept { return attention; }

    TopBar& getDevicePanel() noexcept { return devicePanel; }

    std::function<void (Destination)> onNavigate;
    std::function<void (const juce::String&)> onError;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class State { ok, warn, bad, info, optional };

    struct Check
    {
        juce::String title, detail;
        State state = State::info;
        bool required = true;
        Destination destination = Destination::measure;
        juce::String actionText;
    };

    void evaluate();

    UiContext& ctx;
    TopBar devicePanel;
    std::vector<Check> checks;
    juce::OwnedArray<juce::TextButton> actionButtons;
    juce::Rectangle<int> checklistArea;
    juce::TextButton startButton;
    juce::String signature;
    bool ready = false, attention = false, inputVerified = false;
};

} // namespace eeq::ui
