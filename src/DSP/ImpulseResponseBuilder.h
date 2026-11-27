#pragma once

#include "TransferFunctionAccumulator.h"

#include <vector>

namespace eeq::dsp
{

// H(f) の逆FFTで求めたインパルス応答（仕様 3.3）。
struct ImpulseResponse
{
    int fftSize = 0;
    double sampleRate = 48000.0;

    std::vector<float> samples;      // 長さ fftSize。フラットな H=1 なら t=0 に単位インパルス
    double peakTimeSamples = 0.0;    // 最大ピークの位置（放物線補間済みのサンプル位置）
    double peakValue = 0.0;          // ピークの振幅
};

// IR 生成時の bin の扱い（#115）。
// Ref にエネルギーが無い bin や γ² が低い bin の H1 は |H1| ≈ √(Gyy/Gxx) が巨大になり、
// IFFT すると真のピークより大きな雑音になる。そこで IFFT の前にこれらの bin を 0 にし、
// 有効な bin との境目には余弦テーパーをかける（リンギング抑制）。
struct ImpulseResponseOptions
{
    float refFloorDbBelowPeak = 80.0f; // Ref パワーがピークからこの dB 以上低い bin は除外（OctaveSmoother と同じ）
    float minCoherence = 0.1f;          // γ² がこれ未満の bin は除外（平均2回以上のときだけ判定）
    int taperBins = 0;                  // 境目のテーパー幅 [bin]。0 なら fftSize/1024（最小 4）
};

/** SpectrumFrame（H1 複素）からインパルス応答を組み立てる。純粋関数。 */
[[nodiscard]] ImpulseResponse buildImpulseResponse (const SpectrumFrame& frame,
                                                    const ImpulseResponseOptions& options = {});

} // namespace eeq::dsp
