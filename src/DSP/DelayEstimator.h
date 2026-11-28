#pragma once

#include <algorithm>
#include <cmath>
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
    - minConfidence: 有効とみなす利用側の最小 confidence（既定 5.0）。信号長に応じたノイズ棄却下限と
      6 dB以上のピーク/サイドローブ比も常に適用するため、この値を下げても無相関ノイズは有効にならない。
    - 純粋関数（スレッドを作らない）のでテストから直接使える。
    - mainLobeGuard: ピークの前後この bin 数はメインローブとしてサイドローブ判定から除く。
      実際のスピーカーはマルチウェイ・DSP でインパルスが数 ms に広がるため、4 bin では隣の山を
      「同じ高さのサイドローブ」と数えて実機で常に無効になっていた（MacBook のスピーカーで PSR 0 dB 前後）。
      MeasurementEngine はサンプルレートから 1.5 ms 相当を渡す。 */
[[nodiscard]] DelayEstimate estimateDelay (const float* ref, const float* meas, int numSamples,
                                           bool usePhat, double minConfidence = 5.0, int mainLobeGuard = 4);

/** 循環列 data[0..size) の |data| 最大点 peakIndex の周りで、サブサンプルのピーク位置
    （peakIndex からのずれ。−0.5..0.5）を求める（#118）。
    1. 符号付きの値で放物線補間する（ピークが負なら符号を反転）。|r| に放物線を当てると、
       sinc 形の相関で隣の負の bin の符号が反転し、真の小数部 0.3 が 0.074 になっていた。
    2. 放物線は sinc 形のピークに対して最大 0.12 サンプル程度の偏りが残るため、
       窓付き sinc で帯域制限補間した連続波形の最大点を黄金分割探索で求めて仕上げる。 */
[[nodiscard]] inline double subsamplePeakOffset (const float* data, int size, int peakIndex)
{
    if (data == nullptr || size < 3)
        return 0.0;

    auto at = [&] (int offset)
    {
        auto index = (peakIndex + offset) % size;

        if (index < 0)
            index += size;

        return (double) data[(size_t) index];
    };

    const auto sign = at (0) < 0.0 ? -1.0 : 1.0;
    const auto y0 = sign * at (-1);
    const auto y1 = sign * at (0);
    const auto y2 = sign * at (1);
    const auto denom = y0 - 2.0 * y1 + y2;
    auto parabolic = denom < 0.0 ? 0.5 * (y0 - y2) / denom : 0.0;
    parabolic = std::clamp (parabolic, -0.5, 0.5);

    // 帯域制限補間（Hann 窓付き sinc、片側 halfLength 点）
    const int halfLength = std::min (32, (size - 1) / 2);

    if (halfLength < 2)
        return parabolic;

    constexpr double pi = 3.14159265358979323846;

    auto interpolated = [&] (double t)
    {
        double sum = 0.0;

        for (int k = -halfLength; k <= halfLength; ++k)
        {
            const auto x = t - (double) k;
            const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const auto w = 0.5 * (1.0 + std::cos (pi * x / (double) (halfLength + 1)));
            sum += sign * at (k) * sinc * w;
        }

        return sum;
    };

    // 放物線の推定値を挟む区間で黄金分割探索（ピーク近傍は単峰）
    auto lo = std::max (-0.5, parabolic - 0.25);
    auto hi = std::min (0.5, parabolic + 0.25);
    const auto ratio = 0.5 * (std::sqrt (5.0) - 1.0);
    auto a = hi - ratio * (hi - lo);
    auto b = lo + ratio * (hi - lo);
    auto fa = interpolated (a);
    auto fb = interpolated (b);

    for (int iteration = 0; iteration < 40; ++iteration)
    {
        if (fa < fb)
        {
            lo = a;
            a = b;
            fa = fb;
            b = lo + ratio * (hi - lo);
            fb = interpolated (b);
        }
        else
        {
            hi = b;
            b = a;
            fb = fa;
            a = hi - ratio * (hi - lo);
            fa = interpolated (a);
        }
    }

    return std::clamp (0.5 * (lo + hi), -0.5, 0.5);
}

} // namespace eeq::dsp
