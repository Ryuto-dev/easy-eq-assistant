#pragma once

#include "Core/JuceModules.h"

#include "UiContext.h"

#include <functional>
#include <memory>
#include <vector>

namespace eeq::ui
{

// 「システムに入れる値」（2026-11 リデザイン）。
// 測る（測定）→ 計算する（遅延・位相 / EQ）→ 入れる（卓・プロセッサー）の最後の段。
// 系統ごとに、ディレイ・極性・EQ と「卓に入力済み」のチェックを 1 か所にまとめる。
// 値は SpeakerSystemInfo（会場データ）に保存され、プロジェクトと一緒に残る。

/** 系統の「入れる値」を書き込む共通処理（遅延画面・EQ 画面の「記録」ボタンから呼ぶ） */
namespace applyvalues
{
    /** 名前の系統を返す。無ければ作る（name が空なら「全体」） */
    eeq::core::SpeakerSystemInfo& systemNamed (UiContext& ctx, const juce::String& name);
    void recordDelay (UiContext& ctx, const juce::String& systemName, double delayMs, const juce::String& note);
    void recordPolarity (UiContext& ctx, const juce::String& systemName, bool invert);
    void recordEq (UiContext& ctx, const juce::String& systemName, const std::vector<eeq::dsp::peq::Band>& bands);
    /** 全系統の入れる値をテキストにする（書き出し・コピー用） */
    juce::String sheetAsText (UiContext& ctx);
    juce::String bandTypeName (eeq::dsp::peq::Band::Type type);
}

// 測定・遅延・EQ 画面の右下に出す要約カード
class ApplySummaryPanel : public juce::Component
{
public:
    explicit ApplySummaryPanel (UiContext& context);

    void update();
    std::function<void()> onOpenSheet;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    UiContext& ctx;
    juce::TextButton openButton, recordEqButton;
    juce::String signature;
};

// 「入れる値」画面
class ApplySheetPage : public juce::Component
{
public:
    explicit ApplySheetPage (UiContext& context);
    ~ApplySheetPage() override;

    /** 30Hz（表示中だけ）。系統の増減・値の変化で作り直す */
    void timerTick();

    std::function<void()> onGoToDelay;
    std::function<void()> onGoToEq;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SystemCard;

    void rebuildCards();
    juce::String dataSignature() const;

    UiContext& ctx;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<SystemCard>> cards;
    juce::TextButton addSystemButton, exportButton, copyButton;
    juce::String lastSignature;
};

} // namespace eeq::ui
