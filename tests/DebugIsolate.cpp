// 回帰テスト: メンバーの既定 FFTサイズ（8192）と同じ値で最初に configure したとき、
// バッファ確保がスキップされて空のvectorに書き込むクラッシュが起きていた（仕様 3.1 の
// 8192 を選ぶと即クラッシュする実害があった）。すべての FFTサイズで初期化されることを保証する。
#include <gtest/gtest.h>

#include "DSP/TransferFunctionAccumulator.h"

#include <cmath>
#include <random>
#include <vector>

namespace
{
std::vector<float> makeNoise (int count, unsigned seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    std::vector<float> signal ((size_t) count);

    for (auto& sample : signal)
        sample = dist (rng) * 0.3f;

    return signal;
}
} // namespace

TEST (TransferFunctionRegression, EveryFftSizeInitializesBuffersBeforeFirstFrame)
{
    for (int fftSize : { 256, 2048, 4096, 8192, 16384, 32768, 65536 })
    {
        SCOPED_TRACE (juce::String (fftSize).toStdString());

        eeq::dsp::TransferFunctionAccumulator accumulator;
        accumulator.configure (fftSize, 48000.0, { eeq::dsp::AveragingConfig::Mode::cumulative, 64 },
                               juce::dsp::WindowingFunction<float>::hann);

        const auto count = fftSize * 3; // 最初のフレームが確実に処理される量
        const auto signal = makeNoise (count, 5);
        std::vector<float> shifted ((size_t) count, 0.0f);

        for (int i = 100; i < count; ++i)
            shifted[(size_t) i] = signal[(size_t) (i - 100)];

        const auto block = 1024;

        for (size_t start = 0; start < signal.size(); start += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, signal.size() - start);
            accumulator.feedSamples (signal.data() + start, shifted.data() + start, n);
        }

        eeq::dsp::SpectrumFrame frame;
        accumulator.copyResult (frame);

        EXPECT_EQ (frame.averages, 5); // 3N サンプル / 50%オーバーラップ → 5フレーム
        EXPECT_EQ ((int) frame.transfer.size(), fftSize / 2 + 1);
    }
}
