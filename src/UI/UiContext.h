#pragma once

#include "../Audio/AudioEngine.h"
#include "../Core/ProjectStore.h"
#include "../Core/SystemCorrection.h"
#include "../DSP/OctaveSmoother.h"
#include "../DSP/PEQ.h"
#include "Theme.h"

#include <JuceHeader.h>

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
    std::atomic<uint32_t> snapshotVersion { 0 };     // スナップショット一覧の更新カウンタ
    std::atomic<uint32_t> eqVersion { 0 };           // ターゲット/予測カーブの更新カウンタ

    // 以下はメッセージスレッドだけから触る
    int smoothingDenom = 6;                          // 測定パネルのスムージング
    juce::String currentSystemName;                  // 励起対象の系統名（スナップショットに自動で付く）
    std::vector<eeq::dsp::GridPoint> targetPoints;    // ターゲット曲線（EQ案タブが更新）
    std::vector<eeq::dsp::GridPoint> predictionPoints; // EQ予測（補正後）カーブ
    std::vector<eeq::dsp::peq::Band> proposedBands;   // 補正EQ案（EQ案タブが更新）
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
