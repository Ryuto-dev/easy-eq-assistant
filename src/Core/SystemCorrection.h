#pragma once

#include "../DSP/TransferFunctionAccumulator.h"

#include <juce_core/juce_core.h>

#include <complex>

namespace eeq::core
{

// システム補正（ループバック校正。仕様 6.1）。
// インターフェースの出力→入力をケーブルで戻して測定した H_loopback と往復遅延を保存し、
// 以降の測定で自動で差し引く。補正の対象は出力〜入力の電気系統だけで、
// マイクとスピーカーは含まない。
class SystemCorrection
{
public:
    /** ループバック測定の結果を保存する */
    void setCorrection (const eeq::dsp::SpectrumFrame& loopbackFrame, double roundtripDelaySamples)
    {
        const juce::ScopedLock sl (lock);
        frame = loopbackFrame;
        delaySamples = roundtripDelaySamples;
        active = true;
        measuredAt = juce::Time::getCurrentTime();
    }

    void clear()
    {
        const juce::ScopedLock sl (lock);
        frame = {};
        delaySamples = 0.0;
        active = false;
    }

    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }
    double getRoundtripDelaySamples() const noexcept { return delaySamples.load (std::memory_order_relaxed); }
    juce::Time getMeasuredAt() const { return measuredAt; }

    /** ループバックの FFTサイズ/サンプルレートが現在の測定と一致しているか。
        変わっていたら再測定を促す（仕様 6.1） */
    bool matches (const eeq::dsp::SpectrumFrame& current) const
    {
        const juce::ScopedLock sl (lock);
        return active && frame.fftSize == current.fftSize && std::abs (frame.sampleRate - current.sampleRate) < 0.5;
    }

    /** 測定結果へ補正を適用する。H_corr = H / H_loopback。
        ループバックのコヒーレンスが低い bin（電気系として信頼できない）はそのまま通す。
        removeDelay: 往復遅延を位相から差し引く（平面遅延の除去） */
    void apply (const eeq::dsp::SpectrumFrame& input, eeq::dsp::SpectrumFrame& out, bool removeDelay) const
    {
        const juce::ScopedLock sl (lock);
        out = input;

        if (! active || frame.fftSize != input.fftSize
            || std::abs (frame.sampleRate - input.sampleRate) > 0.5)
            return;

        const auto halfBins = (int) input.transfer.size();
        const auto binWidth = input.sampleRate / (double) input.fftSize;

        for (int k = 1; k < halfBins; ++k)
        {
            const auto& loop = frame.transfer[(size_t) k];
            const auto loopMag = std::abs (std::complex<double> (loop.real(), loop.imag()));

            // ループバックの信号がない bin、コヒーレンスが低い bin は補正しない
            if (loopMag < 1.0e-9 || frame.coherence[(size_t) k] < 0.6)
                continue;

            const auto h = std::complex<double> (input.transfer[(size_t) k].real(),
                                                 input.transfer[(size_t) k].imag());
            const auto corrected = h / std::complex<double> (loop.real(), loop.imag());

            if (removeDelay)
            {
                // H_loopback に含まれる往復遅延を差し引く: e^{+j2πf·Δ} を掛ける
                const auto freq = (double) k * binWidth;
                const auto phase = 2.0 * juce::MathConstants<double>::pi * freq
                                 * delaySamples.load (std::memory_order_relaxed) / input.sampleRate;
                const auto compensation = std::polar (1.0, phase);
                out.transfer[(size_t) k] = { (float) (corrected * compensation).real(),
                                             (float) (corrected * compensation).imag() };
            }
            else
            {
                out.transfer[(size_t) k] = { (float) corrected.real(), (float) corrected.imag() };
            }

            out.magnitudeDb[(size_t) k] = (float) (20.0 * std::log10 (std::max (std::abs (corrected), 1e-9)));
            out.phaseDeg[(size_t) k] = (float) (std::arg (corrected) * 180.0 / juce::MathConstants<double>::pi);
        }
    }

private:
    mutable juce::CriticalSection lock;
    eeq::dsp::SpectrumFrame frame;
    std::atomic<double> delaySamples { 0.0 };
    std::atomic<bool> active { false };
    juce::Time measuredAt;
};

} // namespace eeq::core
