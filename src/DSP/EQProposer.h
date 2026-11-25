#pragma once

#include "PEQ.h"

#include <vector>

namespace eeq::dsp
{

// 補正EQ案の計算への入力（仕様 4.1 / 4.2 / 4.3）。
// カーブは測定・ターゲットともに同じ周波数グリッド（対数グリッド推奨）でそろえて渡す。
struct EQProposalInput
{
    std::vector<double> frequency;  // Hz（昇順）
    std::vector<double> measuredDb;
    std::vector<double> targetDb;
    std::vector<double> weight;     // 0..1。コヒーレンスしきい値未満の帯域は 0（誤差0扱い）

    double sampleRate = 48000.0;
    double minFrequency = 40.0;     // 補正対象の下限（既定 40 Hz）
    double maxFrequency = 16000.0;  // 補正対象の上限（既定 16 kHz）

    int maxBands = 8;               // 最大帯域数（仕様 4.3）
    double cutLimitDb = -12.0;      // ゲイン範囲
    double boostLimitDb = 3.0;
    double qMin = 0.5;
    double qMax = 10.0;
    double minSpacingOctaves = 1.0 / 3.0; // 帯域間の最小間隔
    double residualToleranceDb = 1.5;     // 収束条件（±1.5 dB）
    bool allowValleyBoost = false;  // 谷（キャンセレーション）へのブースト。既定で無効
    double boostScale = 0.7;        // ブースト時のゲイン係数（カットを優先: 仕様 4.2 (3)）
};

// 補正EQ案の計算結果。
struct EQProposalResult
{
    std::vector<peq::Band> bands;
    std::vector<double> predictedDb; // 提案を全て適用した予測カーブ（入力グリッドと同じ長さ）
    std::vector<double> residualDb;  // ターゲット − 予測
    int iterations = 0;
    bool converged = false;
};

/** 反復法で補正EQ案を作る（仕様 4.2）。純粋関数。 */
[[nodiscard]] EQProposalResult proposeEQ (const EQProposalInput& input);

} // namespace eeq::dsp
