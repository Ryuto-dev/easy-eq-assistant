#pragma once

#include <vector>

namespace eeq::dsp
{

// 遅延推定の結果（仕様 3.3）。
struct DelayEstimate
{
    double delaySamples = 0.0;  // Meas が Ref より遅れる量（正）。放物線補間でサブサンプル精度
    double confidence = 0.0;    // 相互相関ピークの目立ち度（ピーク/平均。大きいほど確からしい）
    double peakToSidelobe = 0.0; // ピーク/最大サイドローブ比（dB）。反射環境での判別用
    bool valid = false;
};

/** Ref と Meas のブロックから遅延を推定する（仕様 3.3）。
    - numSamples は 2 の冪（FFT サイズそのまま）。循環畳み込みなので、
      実遅延が numSamples/2 を超えると負側に折り返す点に注意（通常の測定では問題ない）。
    - usePhat: 位相変換付き（GCC-PHAT）。反射の多い環境でピークが鋭くなる。
    - minConfidence: 有効とみなす最小 confidence（既定 5.0）。ノイズ相関（≈1）と区別する。
    - 純粋関数（スレッドを作らない）のでテストから直接使える。 */
[[nodiscard]] DelayEstimate estimateDelay (const float* ref, const float* meas, int numSamples,
                                           bool usePhat, double minConfidence = 5.0);

} // namespace eeq::dsp
