#pragma once

#include "Core/JuceModules.h"

#include "../DSP/ImpulseResponseBuilder.h"
#include "Strings.h"
#include "UiContext.h"

#include <memory>

namespace eeq::ui
{

// IR再構築用のバックグラウンドワーカー（issue #65: UIスレッドから重いFFTを移動）
class IrRebuildWorker final : private juce::Thread
{
public:
    struct Request
    {
        std::shared_ptr<const eeq::dsp::SpectrumFrame> frame;
    };

    struct Result
    {
        std::unique_ptr<eeq::dsp::ImpulseResponse> ir;
    };

    IrRebuildWorker() : juce::Thread ("IR reconstruction") {}
    ~IrRebuildWorker() override { stopThread (-1); }

    void start() { startThread (juce::Thread::Priority::low); }

    void submit (Request request)
    {
        const juce::ScopedLock sl (lock);
        pending = std::move (request);
        hasPending = true;
    }

    bool takeResult (Result& result)
    {
        const juce::ScopedLock sl (lock);
        if (! hasResult) return false;
        result = std::move (completed);
        hasResult = false;
        return true;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            Request request;
            bool haveRequest = false;
            {
                const juce::ScopedLock sl (lock);
                if (hasPending)
                {
                    request = std::move (pending);
                    hasPending = false;
                    haveRequest = true;
                }
            }
            if (! haveRequest)
            {
                juce::Thread::sleep (4);
                continue;
            }
            if (! request.frame) continue;

            Result result;
            result.ir = std::make_unique<eeq::dsp::ImpulseResponse> (eeq::dsp::buildImpulseResponse (*request.frame));

            {
                const juce::ScopedLock sl (lock);
                completed = std::move (result);
                hasResult = true;
            }
        }
    }

    juce::CriticalSection lock;
    Request pending;
    bool hasPending = false;
    Result completed;
    bool hasResult = false;
};

// 「IR・遅延」タブ（仕様 3.3）。
// - 相互相関による遅延検出（GCC-PHAT切替・放物線補間）、遅延の適用（位相補正）
// - インパルス応答の表示（時間軸とETC。ピーク位置を0 msに揃えられる）
// - ループバック校正（システム補正。仕様 6.1）
// - 系統の管理と個別測定（仕様 3.3.4）、系統比較（ディレイタワー3.3.1 / サブの位相合わせ3.3.3 /
//   ステージ上マイク3.3.5）
class DelayIrTab : public juce::Component
{
public:
    explicit DelayIrTab (UiContext& context);
    ~DelayIrTab() override;

    void timerTick();

    /** 非表示タブ用の軽量ティック: ループバック測定の進行・自動停止だけを行う（issue #55） */
    void timerTickBackground();

    void resized() override;
    void paint (juce::Graphics&) override;

    /** 下部バーの「遅延測定」・D キーから呼ぶ。相互相関で遅延を検出して表示する。
        forceApply = true か「検出したら適用」がオンなら、有効な結果をそのまま適用する（issue #142） */
    void runDelayMeasurement (bool forceApply = false);

    void mouseDown (const juce::MouseEvent&) override;

    /** ループバック補正の状態表示を最新化する（起動時の復元後にMainComponentから呼ぶ） */
    void refreshLoopbackStatus() { updateLoopbackUi(); }

private:
    // ---- 遅延 ----
    void updateDelayReadout();
    void applyDelay();
    void clearDelay();
    void refreshDistanceFromManualDelay();
    void trackDelayStep(); // Track（追従）: 測定中に定期的に再推定して追従する（issue #142）
    void setAppliedDelayMs (double ms, const juce::String& reason);

    // ---- IR ----
    void requestIrRebuild (const eeq::dsp::SpectrumFrame& frame);
    void checkIrRebuildResult();

    // ---- ループバック校正 ----
    void measureLoopback();
    void finishLoopbackMeasurement();
    void processLoopbackRanging();
    void updateLoopbackUi();

    // ---- 系統 ----
    void rebuildSystemList();
    void setExciteTarget (int index); // -1 = 全体（合成）
    void saveSystemsToVenue();

    // ---- 系統比較 ----
    void updateCompare();

    UiContext& ctx;

    // 遅延の状態
    eeq::dsp::DelayEstimate lastEstimate;
    bool hasEstimate = false;
    bool usePhat = false;
    double tempAtDelayApply = 999.0; // 遅延適用時の気温（温度ずれ予測の基準。仕様 3.8）

    // IRのキャッシュ
    std::unique_ptr<eeq::dsp::ImpulseResponse> cachedIr;
    int cachedIrAverages = -1;
    juce::Time irRebuildAllowed; // IR再構築のスロットル用（issue #37）
    int cachedIrWidth = -1;
    int cachedIrFftSize = 0;
    double cachedIrSampleRate = 0.0;
    bool peakAligned = true;
    bool showEtc = true;
    juce::Range<float> irTimeRangeMs { -20.0f, 100.0f };

    // IR再構築ワーカー（バックグラウンド）
    IrRebuildWorker irWorker;

    // 系統の選択（-1 = 全体）
    int exciteTarget = -1;
    int compareVersionSeen = -1;
    std::unique_ptr<eeq::dsp::ImpulseResponse> compareIrA, compareIrB; // 比較用 IR のキャッシュ（issue #138）
    std::uint64_t compareIrIdA = 0, compareIrIdB = 0;
    int irGraphHeight = 180;

    // ループバック校正の自動進行（issue #17/#31）
    bool loopbackRanging = false;
    juce::Time loopbackDeadline;

    // Issue #142: Delay Finder 改善
    bool autoApplyDelay = false;     // 検出即適用
    bool trackFollowEnabled = false; // トラック追従
    bool irClipHold = false;         // IR保持
    std::unique_ptr<eeq::dsp::ImpulseResponse> heldIr; // 保持中のIR
    int trackTickCounter = 0;
    // 測定開始時の遅延の自動設定: 遅延が未設定（0）のまま測定を始めたら、数秒後に一度だけ検出して適用する。
    // 遅延を合わせないと位相が回り続け（ぐちゃぐちゃに見える）、FFT 窓のずれでコヒーレンスも下がるため
    bool wasMeasuringForAutoDelay = false;
    bool autoDelayPending = false;
    int autoDelayTicks = 0;
    void autoDelayStep();
    juce::Rectangle<float> lastIrArea;     // IR グラフの描画領域（クリックで遅延を指定）
    double lastIrT0 = 0.0, lastIrT1 = 0.0; // IR グラフの時間範囲（ms）
    juce::Label trackStatusLabel;

    // ---- 遅延パネル ----
    juce::Label delayTitle, delayValueLabel, delayProposalLabel, distanceCheckLabel;
    juce::TextButton measureButton, applyButton, clearButton, manualApplyButton;
    juce::ToggleButton phatToggle, rawPhaseToggle, peakAlignToggle, etcToggle;
    juce::ToggleButton autoApplyToggle;   // 検出即適用（issue #142）
    juce::ToggleButton trackFollowToggle; // トラック追従（issue #142）
    juce::ToggleButton irHoldToggle;      // IR保持（issue #142）

    // 手動入力（ms と m が連動: 仕様 3.3.2）
    juce::Label manualDelayTitle, manualDistanceTitle;
    juce::TextEditor manualDelayEditor, manualDistanceEditor;
    juce::Label manualWarnLabel;

    // ---- IR表示 ----
    juce::Label irTitle;
    juce::TextButton irExportButton; // IR を WAV (32bit float) で書き出す（仕様 6.2）

    // ---- ループバック校正 ----
    juce::Label loopbackTitle, loopbackStatusLabel;
    juce::TextButton loopbackMeasureButton, loopbackClearButton;
    juce::ToggleButton loopbackEnableToggle;

    // ---- 系統管理 ----
    juce::Label systemsTitle, systemsHint;
    juce::TextButton systemsAddButton, systemsRemoveButton, systemsAllButton, systemsTargetButton;
    juce::ListBox systemList { {}, nullptr };
    juce::TextEditor systemNameEditor;    // 選択中の系統名を編集（issue #15）
    juce::ComboBox systemChannelCombo;    // 選択中の系統の出力chを編集（issue #15）
    uint32_t seenDeviceGenerationForChannels = 0;

    void refreshSystemEditors();
    void refreshChannelComboChoices();

    // ---- 系統比較 ----
    juce::Label compareTitle, compareARowLabel, compareBRowLabel;
    juce::ComboBox compareComboA, compareComboB;
    juce::Label compareResultLabel, compareTowerLabel, comparePolarityLabel, compareCrossoverLabel;
    juce::Label compareFineLabel;
    juce::Slider compareFineSlider;

    class SystemListModel;
    std::unique_ptr<SystemListModel> systemListModel;
};

} // namespace eeq::ui
