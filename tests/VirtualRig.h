#pragma once

// 仮想オーディオ機器による E2E テストハーネス（ロードマップ A-1）。
// AudioEngine のオーディオコールバックをオフラインで回し、
//   アプリの出力 → 既知の系（遅延・ゲイン・PEQ・雑音・クロックずれ）→ 入力
// を数値で通す。AudioEngine → リングバッファ → MeasurementEngine（解析スレッド）→ 結果 までを丸ごと検証できる。
// 「部品は正しいのに、つなぐと間違う」種類のバグ（#106 / #108 / #109 / #113）を捕まえるためのもの。

#include "Audio/AudioEngine.h"
#include "DSP/PEQ.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <cmath>
#include <complex>
#include <random>
#include <thread>
#include <vector>

namespace eeq::audio
{
// AudioEngine の private なコールバックを呼ぶための入口（AudioEngine.h の friend）
struct AudioEngineCallbackTestAccess
{
    static void prepare (AudioEngine& engine, juce::AudioIODevice& device) { engine.audioDeviceAboutToStart (&device); }

    static void render (AudioEngine& engine, float* output, int samples)
    {
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, &output, 1, samples, {});
    }

    static void render2 (AudioEngine& engine, float* out0, float* out1, int samples)
    {
        float* outs[] = { out0, out1 };
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, samples, {});
    }

    static void renderWithInputs (AudioEngine& engine, const float* const* inputs, int numInputs, float* const* outputs,
                                  int numOutputs, int samples)
    {
        engine.audioDeviceIOCallbackWithContext (inputs, numInputs, outputs, numOutputs, samples, {});
    }

    static void stopDevice (AudioEngine& engine) { engine.audioDeviceStopped(); }
};
}

namespace eeq::test
{

/** 入出力 2ch ずつのテスト用デバイス。サンプルレートとブロック長は自由に決められる */
class VirtualDevice final : public juce::AudioIODevice
{
public:
    VirtualDevice (double rate, int block) : AudioIODevice ("Virtual rig", "Test"), sampleRate (rate), blockSize (block) {}

    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return { "In 1", "In 2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { sampleRate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { blockSize }; }
    int getDefaultBufferSize() override { return blockSize; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return juce::BigInteger (3); }
    juce::BigInteger getActiveInputChannels() const override { return juce::BigInteger (3); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    const double sampleRate;
    const int blockSize;
};

/** スピーカー〜部屋〜マイクの代わりの「既知の系」 */
struct KnownSystem
{
    int extraDelaySamples = 0;          // 追加の遅延（実際の遅延はこれ＋ブロック長。同じコールバック内で入出力はつながらないため）
    double gainDb = 0.0;
    std::vector<eeq::dsp::peq::Band> filters;
    double noiseRms = 0.0;              // 入力に足す白色雑音（線形 RMS）
    double clockDriftPpm = 0.0;         // 入力側のクロックが出力よりこれだけ速い（ppm）

    /** 真の伝達関数（遅延を除く） */
    std::complex<double> response (double f, double rate) const
    {
        std::complex<double> h = std::pow (10.0, gainDb / 20.0);
        const auto w = 2.0 * juce::MathConstants<double>::pi * f / rate;
        for (const auto& band : filters)
        {
            const auto c = eeq::dsp::peq::coeffsFor (band, rate);
            const auto z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            h *= (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        }
        return h;
    }
};

/** 仮想デバイス＋既知の系で AudioEngine を回す */
class VirtualRig
{
public:
    using Access = eeq::audio::AudioEngineCallbackTestAccess;

    VirtualRig (double rate, int block, KnownSystem systemToUse)
        : device (rate, block), engine (nullptr), system (std::move (systemToUse)), rng (12345)
    {
        for (const auto& band : system.filters)
            biquads.push_back ({ eeq::dsp::peq::coeffsFor (band, rate) });

        Access::prepare (engine, device);
        engine.setMeasChannel (0);
        engine.setRefChannel (eeq::audio::AudioEngine::internalRefChannel);
        engine.setOutputRoutingMask (1);
        out0.assign ((size_t) block, 0.0f);
        out1.assign ((size_t) block, 0.0f);
        in0.assign ((size_t) block, 0.0f);
        in1.assign ((size_t) block, 0.0f);
    }

    ~VirtualRig() { Access::stopDevice (engine); }

    /** 実際の遅延（サンプル）= 追加の遅延＋1 ブロック */
    int totalDelaySamples() const noexcept { return system.extraDelaySamples + device.blockSize; }

    /** seconds 秒ぶん回す。解析スレッドが追いつくよう、ときどき待つ（実時間の数倍の速さ） */
    void run (double seconds)
    {
        const auto blocks = (int) std::ceil (seconds * device.sampleRate / device.blockSize);
        std::normal_distribution<double> gauss (0.0, 1.0);

        for (int b = 0; b < blocks; ++b)
        {
            // 入力: 系の出力 y を、遅延（とクロックずれ）ぶん前から読む。y はこれまでの出力から計算済み
            for (int i = 0; i < device.blockSize; ++i)
            {
                const auto t = (double) (written + i) * (1.0 + system.clockDriftPpm * 1.0e-6) - (double) totalDelaySamples();
                in0[(size_t) i] = (float) (readSystemOutput (t) + system.noiseRms * gauss (rng));
                in1[(size_t) i] = 0.0f;
            }

            engine.kickUiWatchdog();
            const float* inputs[] = { in0.data(), in1.data() };
            float* outputs[] = { out0.data(), out1.data() };
            Access::renderWithInputs (engine, inputs, 2, outputs, 2, device.blockSize);

            // 出力を系に通して y に貯める
            for (int i = 0; i < device.blockSize; ++i)
            {
                double v = out0[(size_t) i] * std::pow (10.0, system.gainDb / 20.0);
                for (auto& bq : biquads)
                    v = bq.process (v);
                y.push_back (v);
            }
            written += device.blockSize;

            if (b % 8 == 7)
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (60)); // 解析スレッドが最後のフレームを出すまで
    }

    eeq::audio::AudioEngine& getEngine() noexcept { return engine; }
    const KnownSystem& getSystem() const noexcept { return system; }
    double getSampleRate() const noexcept { return device.sampleRate; }

private:
    struct Biquad
    {
        eeq::dsp::peq::Coeffs c;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        double process (double x)
        {
            const auto out = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = out;
            return out;
        }
    };

    double readSystemOutput (double t) const
    {
        if (t < 0.0)
            return 0.0;
        const auto i0 = (size_t) t;
        const auto frac = t - (double) i0;
        if (i0 + 1 >= y.size())
            return i0 < y.size() ? y[i0] : 0.0;
        return y[i0] + (y[i0 + 1] - y[i0]) * frac; // クロックずれのときの小数位置は線形補間
    }

    VirtualDevice device;
    eeq::audio::AudioEngine engine;
    KnownSystem system;
    std::vector<Biquad> biquads;
    std::vector<double> y;
    std::vector<float> out0, out1, in0, in1;
    std::mt19937 rng;
    long long written = 0;
};

} // namespace eeq::test
