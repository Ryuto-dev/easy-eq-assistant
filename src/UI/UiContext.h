#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/MicCalibration.h"
#include "../Core/ProjectStore.h"
#include "../Core/SystemCorrection.h"
#include "../DSP/OctaveSmoother.h"
#include "../DSP/PEQ.h"
#include "Theme.h"

#include "Core/JuceModules.h"

#include <atomic>
#include <vector>

namespace eeq::ui
{

// タブ間で共有するUI側の状態（位相の遅延補正値など。測定そのものは AudioEngine が持つ）
struct UiState
{
    std::atomic<double> appliedDelaySamples { 0.0 }; // 位相表示の遅延補正（仕様 3.3）
    std::atomic<bool> showRawPhase { false };        // 補正前の生位相に切り替え（仕様 3.1）
    std::atomic<double> manualDelayMs { 0.0 };       // 手動入力の遅延（ms。仕様 3.3.2）
    std::atomic<double> airTemperatureC { 20.0 };    // 気温（既定 20 ℃。仕様 3.3.2）
    std::atomic<bool> loopbackEnabled { true };      // システム補正のON/OFF（仕様 6.1）
    std::atomic<uint32_t> snapshotVersion { 0 };     // スナップショット一覧の更新カウンタ
    std::atomic<uint32_t> eqVersion { 0 };           // ターゲット/予測カーブの更新カウンタ
    // Ref 遅延補償を一時的に 0 にする（ループバック校正中。往復遅延だけの系を測るため: issue #106）
    std::atomic<bool> suspendReferenceDelay { false };

    // 以下はメッセージスレッドだけから触る
    int smoothingDenom = 6;                          // 測定パネルのスムージング
    juce::String currentSystemName;                  // 励起対象の系統名（スナップショットに自動で付く）
    std::vector<eeq::dsp::GridPoint> targetPoints;    // ターゲット曲線（EQ案タブが更新）
    std::vector<eeq::dsp::GridPoint> predictionPoints; // EQ予測（補正後）カーブ
    std::vector<eeq::dsp::peq::Band> proposedBands;   // 補正EQ案（EQ案タブが更新）
    double targetLevelOffsetDb = 0.0;

    // 測定マイクの補正（ロードマップ B-5）。読み込んだら Meas 側から差し引く（表示・スナップショット・EQ 案）。
    // 不変オブジェクトなので、解析ワーカーへは shared_ptr のコピーで渡す
    std::shared_ptr<const eeq::core::MicCalibration> micCalibration;

    /** 測定結果に、表示と同じ補正（ループバック補正 → マイク補正）をかける */
    void applyMicCalibration (eeq::dsp::SpectrumFrame& frame) const
    {
        if (micCalibration != nullptr)
            micCalibration->applyTo (frame);
    }                 // ターゲットを測定の全体レベルへ合わせた量（issue #109）
};

// タブに渡す共有コンテキスト
struct UiContext
{
    eeq::audio::AudioEngine& engine;
    juce::PropertiesFile& settings;
    eeq::core::ProjectStore& project;
    eeq::core::SystemCorrection& correction;
    UiState& state;
};

} // namespace eeq::ui
