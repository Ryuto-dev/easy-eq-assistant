#pragma once

#include "Core/JuceModules.h"

#include <functional>
#include <vector>

namespace eeq::ui
{

// 左の作業ナビ（2026-11 リデザイン）。
// 上半分は「作業の流れ」: 1 準備 → 2 測定 → 3 遅延・位相 → 4 EQ → 5 入れる値。番号と完了マークで
// 「いまどこにいて、次に何をするか」を示す。下半分は流れに乗らない「道具」（RTA・SPL、上限測定、計算、会場）。
class SideNav : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Item
    {
        int pageId = 0;
        juce::String title;
        juce::String subtitle; // 作業の流れだけ（道具は空）
        int stepNumber = 0;    // 0 = 道具
        bool done = false;     // 作業の流れの完了マーク
        bool attention = false; // 要対応（警告色の点）
    };

    SideNav();

    void setItems (std::vector<Item> newItems);
    void setSelectedPage (int pageId);
    int getSelectedPage() const noexcept { return selectedPage; }

    /** 完了マーク・要対応の点だけを更新する（30Hz で呼んでも変化がなければ再描画しない） */
    void setStepState (int pageId, bool done, bool attention);

    std::function<void (int pageId)> onPageSelected;
    std::function<void()> onThemeToggle;
    std::function<void()> onShortcutHelp;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int preferredWidth = 184;

private:
    int itemAt (juce::Point<int> position) const;
    juce::Rectangle<int> itemBounds (size_t index) const;

    std::vector<Item> items;
    std::vector<juce::Rectangle<int>> layout;
    juce::Rectangle<int> toolsHeader, flowHeader;
    int selectedPage = -1;
    int hoveredIndex = -1;
    juce::TextButton themeButton, helpButton;
};

} // namespace eeq::ui
