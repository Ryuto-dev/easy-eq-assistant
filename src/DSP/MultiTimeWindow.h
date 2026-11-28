#pragma once

#include "TransferFunctionAccumulator.h"

#include <array>
#include <cmath>
#include <complex>
#include <memory>

namespace eeq::dsp
{

// MTW（Multi-Time-Window。ロードマップ B-2）。
// 単一の FFT は窓長が 1 つしかないので、低域の分解能を取ると高域が反射音で汚れ、
// 高域をきれいに取ると低域の分解能が足りない。帯域ごとに窓長を変えて（低域は長く、高域は短く）、
// どの帯域もおよそ 1/30 oct の分解能でつなぐ。Smaart の伝達関数が見やすい一番の理由。
//
// 低域はメインの平均（ユーザーの FFT サイズ）をそのまま使い、短い窓 3 つ（48 kHz で 8192 / 2048 / 512）を
// 並列に平均する。各帯域は「ビン間隔が f/30 以下になる最も短い窓」を使い、境目は 1/6 oct で
// クロスフェードする。合成した結果はメインと同じビン数の SpectrumFrame として返すので、
// 表示・スナップショット・EQ 案はそのまま使える（IR と遅延推定は単一 FFT のフレームを使う）。
//
// 短い窓は Ref/Meas の時間がずれていると成り立たない（ずれが窓長に近いと相関が消える）。
// 遅延を合わせてから使うこと（呼び出し側が判断する）。
class MultiTimeWindow
{
public:
    static constexpr int numShortWindows = 3;
    static constexpr double resolutionRatio = 30.0; // ビン間隔 ≤ f / 30（約 1/21 oct より細かい）

    /** メインの設定に合わせて短い窓を設定する。平均はメインと同じ時間幅になるよう指数平均の回数を決める */
    void configure (int mainFftSize, double sampleRate, const AveragingConfig& mainAveraging,
                    juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod)
    {
        mainSize = mainFftSize;
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        // 48 kHz 基準の窓長をレートに比例させる（96 kHz なら倍）
        const auto scale = juce::jmax (1, (int) std::lround (rate / 48000.0));
        const int baseSizes[numShortWindows] = { 8192, 2048, 512 };

        // メインの平均の時間幅（フレームの間隔は 50% オーバーラップで N/2）
        const auto mainFrames = juce::jlimit (1, 256, mainAveraging.maxAverages);
        const auto spanSamples = (double) mainFrames * (double) mainFftSize * 0.5;

        for (int w = 0; w < numShortWindows; ++w)
        {
            sizes[(size_t) w] = juce::jmin (mainFftSize / 2, baseSizes[w] * scale);
            AveragingConfig config;
            config.mode = AveragingConfig::Mode::exponential;
            config.maxAverages = juce::jlimit (1, 256, (int) std::lround (spanSamples / (sizes[(size_t) w] * 0.5)));
            accumulators[(size_t) w].configure (sizes[(size_t) w], rate, config, windowMethod);
        }
    }

    void resetAverages()
    {
        for (auto& a : accumulators)
            a.resetAverages();
    }

    void feedSamples (const float* ref, const float* meas, int numSamples)
    {
        for (auto& a : accumulators)
            a.feedSamples (ref, meas, numSamples);
    }

    /** 窓 w が受け持ち始める周波数（これ以上はこの窓、以下はより長い窓） */
    double crossoverFor (int w) const noexcept
    {
        return resolutionRatio * rate / (double) sizes[(size_t) w];
    }

    /** メインの結果と短い窓の結果をつないだフレームを作る */
    void compose (const SpectrumFrame& main, SpectrumFrame& out)
    {
        out = main;

        if (main.fftSize <= 0 || main.transfer.empty())
            return;

        for (int w = 0; w < numShortWindows; ++w)
            accumulators[(size_t) w].copyResult (shortResults[(size_t) w]);

        const auto binHz = main.sampleRate / (double) main.fftSize;
        const auto bins = main.transfer.size();

        for (size_t k = 1; k < bins; ++k)
        {
            const auto f = (double) k * binHz;

            // 各境目でのクロスフェード（その境目より上の割合）から、どの窓をどれだけ使うかを決める
            std::complex<double> h = 0.0;
            double coherence = 0.0, refDb = 0.0, measDb = 0.0, used = 0.0;

            // weight[w+1] = 境目 w（短い窓 w の受け持ち開始）より上にある割合。1/6 oct で 0→1
            std::array<double, numShortWindows + 1> weight {};
            for (int w = 0; w < numShortWindows; ++w)
                weight[(size_t) w + 1] = juce::jlimit (0.0, 1.0, std::log2 (f / crossoverFor (w)) * 6.0 + 0.5);

            // weight[i]（i≥1）を「その窓以上の割合」として、各窓の取り分に直す
            std::array<double, numShortWindows + 1> share {};
            share[0] = 1.0 - weight[1];
            for (int w = 1; w <= numShortWindows; ++w)
                share[(size_t) w] = weight[(size_t) w] - (w < numShortWindows ? weight[(size_t) w + 1] : 0.0);

            for (int i = 0; i <= numShortWindows; ++i)
            {
                const auto s = share[(size_t) i];
                if (s <= 1.0e-6)
                    continue;

                std::complex<double> hi;
                double ci = 0.0, ri = 0.0, mi = 0.0;

                if (i == 0)
                {
                    hi = { main.transfer[k].real(), main.transfer[k].imag() };
                    ci = k < main.coherence.size() ? main.coherence[k] : 0.0;
                    ri = k < main.refPowerDb.size() ? main.refPowerDb[k] : 0.0;
                    mi = k < main.measPowerDb.size() ? main.measPowerDb[k] : 0.0;
                }
                else
                {
                    const auto& r = shortResults[(size_t) i - 1];
                    if (r.fftSize <= 0 || r.transfer.size() < 2 || r.averages < 1)
                        continue; // まだ溜まっていない窓は使わない（長い窓の値のまま）

                    const auto pos = f / (r.sampleRate / (double) r.fftSize);
                    const auto i0 = juce::jlimit<size_t> (0, r.transfer.size() - 2, (size_t) pos);
                    const auto t = juce::jlimit (0.0, 1.0, pos - (double) i0);
                    const std::complex<double> a (r.transfer[i0].real(), r.transfer[i0].imag());
                    const std::complex<double> b (r.transfer[i0 + 1].real(), r.transfer[i0 + 1].imag());
                    hi = a + (b - a) * t;
                    auto lerp = [t, i0] (const std::vector<float>& v)
                    { return v.size() > i0 + 1 ? (double) v[i0] + ((double) v[i0 + 1] - (double) v[i0]) * t : 0.0; };
                    ci = lerp (r.coherence);
                    // パワーは窓長に比例するので、メインの窓長に換算する
                    const auto toMain = 10.0 * std::log10 ((double) main.fftSize / (double) r.fftSize);
                    ri = lerp (r.refPowerDb) + toMain;
                    mi = lerp (r.measPowerDb) + toMain;
                }

                h += hi * s;
                coherence += ci * s;
                refDb += ri * s;
                measDb += mi * s;
                used += s;
            }

            if (used <= 1.0e-9)
                continue;

            h /= used;
            out.transfer[k] = { (float) h.real(), (float) h.imag() };
            if (k < out.magnitudeDb.size())
                out.magnitudeDb[k] = (float) (20.0 * std::log10 (std::max (std::abs (h), 1.0e-12)));
            if (k < out.phaseDeg.size())
                out.phaseDeg[k] = (float) (std::arg (h) * 180.0 / juce::MathConstants<double>::pi);
            if (k < out.coherence.size())
                out.coherence[k] = (float) (coherence / used);
            if (k < out.refPowerDb.size())
                out.refPowerDb[k] = (float) (refDb / used);
            if (k < out.measPowerDb.size())
                out.measPowerDb[k] = (float) (measDb / used);
        }
    }

    int getWindowSize (int w) const noexcept { return sizes[(size_t) w]; }
    int getAverages (int w) const { return accumulators[(size_t) w].getAverages(); }

private:
    std::array<TransferFunctionAccumulator, numShortWindows> accumulators;
    std::array<SpectrumFrame, numShortWindows> shortResults;
    std::array<int, numShortWindows> sizes { 8192, 2048, 512 };
    int mainSize = 16384;
    double rate = 48000.0;
};

} // namespace eeq::dsp
