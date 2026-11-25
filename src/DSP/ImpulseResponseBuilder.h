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

/** SpectrumFrame（H1 複素）からインパルス応答を組み立てる。純粋関数。 */
[[nodiscard]] ImpulseResponse buildImpulseResponse (const SpectrumFrame& frame);

} // namespace eeq::dsp
