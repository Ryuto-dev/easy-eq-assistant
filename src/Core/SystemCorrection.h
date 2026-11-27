#pragma once

#include "../DSP/TransferFunctionAccumulator.h"
#include "ProjectStore.h"

#include <juce_core/juce_core.h>

#include <complex>
#include <cstdint>

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
        generationCounter.fetch_add (1, std::memory_order_release);
    }

    void clear()
    {
        const juce::ScopedLock sl (lock);
        frame = {};
        delaySamples = 0.0;
        active = false;
        generationCounter.fetch_add (1, std::memory_order_release);
    }

    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }
    std::uint64_t getGeneration() const noexcept { return generationCounter.load (std::memory_order_acquire); }
    double getRoundtripDelaySamples() const noexcept { return delaySamples.load (std::memory_order_relaxed); }
    juce::Time getMeasuredAt() const { return measuredAt; }

    /** アプリ設定へ保存するためのシリアライズ（issue #21: 再起動でも補正を残す） */
    [[nodiscard]] juce::var toVar() const
    {
        juce::DynamicObject* o = new juce::DynamicObject();
        juce::var object (o);

        const juce::ScopedLock sl (lock);
        o->setProperty ("delaySamples", delaySamples.load (std::memory_order_relaxed));
        o->setProperty ("measuredAt", (juce::int64) measuredAt.toMilliseconds());
        o->setProperty ("frame", ProjectStore::spectrumFrameToVar (frame));

        return object;
    }

    /** 保存した補正を復元する。壊れたデータや空のフレームは false（補正なしのまま） */
    bool fromVar (const juce::var& v)
    {
        auto* o = v.getDynamicObject();

        if (o == nullptr)
            return false;

        auto restored = ProjectStore::spectrumFrameFromVar (o->getProperty ("frame"));

        // frameFromVar は壊れた入力に対して fftSize=0 の空フレームを返す
        if (restored.fftSize < 256)
            return false;

        setCorrection (restored, (double) o->getProperty ("delaySamples"));

        if (const auto ms = (juce::int64) o->getProperty ("measuredAt"); ms != 0)
            measuredAt = juce::Time (ms);

        return true;
    }

    /** ループバックの FFTサイズ/サンプルレートが現在の測定と一致しているか。
        変わっていたら再測定を促す（仕様 6.1） */
    bool matches (const eeq::dsp::SpectrumFrame& current) const
    {
        const juce::ScopedLock sl (lock);
        return active && frame.fftSize == current.fftSize && std::abs (frame.sampleRate - current.sampleRate) < 0.5;
    }

    /** 測定結果へ補正を適用する。H_corr = H / H_loopback。
        ループバックのコヒーレンスが低い bin（電気系として信頼できない）はそのまま通す。
        同じ内部参照で測った測定には H_loop の位相（往復遅延を含む）がそのまま乗っているので、
        複素除算だけでループの遅延も込みで打ち消される（追加の位相補正は入れない。二重に
        差し引くと遅延が −Δ ずれる） */
    void apply (const eeq::dsp::SpectrumFrame& input, eeq::dsp::SpectrumFrame& out, bool removeDelay) const
    {
        juce::ignoreUnused (removeDelay); // 位相は複素除算で自動的に補正される（互換用の引数）

        const juce::ScopedLock sl (lock);
        out = input;

        if (! active || frame.fftSize != input.fftSize
            || std::abs (frame.sampleRate - input.sampleRate) > 0.5)
            return;

        const auto halfBins = (int) input.transfer.size();

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

            out.transfer[(size_t) k] = { (float) corrected.real(), (float) corrected.imag() };
            out.magnitudeDb[(size_t) k] = (float) (20.0 * std::log10 (std::max (std::abs (corrected), 1e-9)));
            out.phaseDeg[(size_t) k] = (float) (std::arg (corrected) * 180.0 / juce::MathConstants<double>::pi);
        }
    }

private:
    mutable juce::CriticalSection lock;
    eeq::dsp::SpectrumFrame frame;
    std::atomic<double> delaySamples { 0.0 };
    std::atomic<bool> active { false };
    std::atomic<std::uint64_t> generationCounter { 0 };
    juce::Time measuredAt;
};

} // namespace eeq::core
