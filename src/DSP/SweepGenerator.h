#pragma once

#include "Core/JuceModules.h"

#include <cmath>
#include <vector>

namespace eeq::dsp
{

// 対数スイープ（Log Sine Sweep、仕様 3.2）。
// ノイズに強くインパルス応答を正確に取れる。セットアップ時の測定用（長さ 2〜30 秒）。
// 生成はUIスレッドで行い、AudioEngine がバッファを再生する。
class LogSweepGenerator
{
public:
    /** 対数スイープを生成する。両端にフェードを付ける。振幅はリニア（0..1） */
    [[nodiscard]] static std::vector<float> generate (double sampleRate, double durationSeconds,
                                                      double startFrequency = 20.0,
                                                      double endFrequency = 20000.0,
                                                      float amplitude = 0.5f,
                                                      double fadeSeconds = 0.05)
    {
        std::vector<float> sweep;

        if (sampleRate <= 0.0 || durationSeconds < 0.1)
            return sweep;

        const auto n = (int) (durationSeconds * sampleRate);
        sweep.resize ((size_t) n);

        const auto f1 = juce::jlimit (1.0, sampleRate * 0.45, startFrequency);
        const auto f2 = juce::jlimit (f1 * 1.0001, sampleRate * 0.49, endFrequency);
        const auto k = 2.0 * juce::MathConstants<double>::pi * f1 * durationSeconds / std::log (f2 / f1);
        const auto l = std::log (f2 / f1) / durationSeconds;

        const auto fadeSamples = (int) juce::jlimit (1.0, durationSeconds * sampleRate * 0.4, fadeSeconds * sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const auto t = (double) i / sampleRate;
            auto sample = amplitude * std::sin (k * (std::exp (l * t) - 1.0));

            // フェードイン/アウト（急な立ち上がりによるリンギングとクリックを防ぐ）
            if (i < fadeSamples)
                sample *= (float) i / (float) fadeSamples;
            else if (i >= n - fadeSamples)
                sample *= (float) (n - 1 - i) / (float) fadeSamples;

            sweep[(size_t) i] = sample;
        }

        return sweep;
    }

    /** スイープの k 次高調波の時間位置（秒）。Farina 法の高調波分離（仕様 3.7）に使う。
        対数スイープでは k 次高調波の応答が t·ln(k)/ln(f2/f1) だけ早く現れる */
    [[nodiscard]] static double harmonicArrivalSeconds (double durationSeconds, double startFrequency,
                                                        double endFrequency, int harmonic)
    {
        const auto l = std::log (endFrequency / startFrequency);

        if (l <= 0.0 || harmonic < 2)
            return 0.0;

        return durationSeconds * std::log ((double) harmonic) / l;
    }
};

} // namespace eeq::dsp
