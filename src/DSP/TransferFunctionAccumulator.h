#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <complex>
#include <vector>

// デュアルFFT（仕様 3.1）の中核。
//   H1推定:  H(f) = Gxy(f) / Gxx(f)   （x = Ref, y = Meas）
//   コヒーレンス: γ²(f) = |Gxy|² / (Gxx · Gyy)
// Welch法（50%オーバーラップ）で Gxx / Gyy / Gxy を平均する。
// 平均は「リセット可能な累積平均」と「指数平均」を weight の切替で実装する。
//
// スレッドを立てない純粋なクラスにしてある（ユニットテストから直接使える）。

namespace eeq::dsp
{

struct AveragingConfig
{
    enum class Mode { cumulative, exponential };

    Mode mode = Mode::cumulative;
    int maxAverages = 64; // 1〜256（仕様 3.1）
};

// 解析結果（binごと・未スムージング）。スムージングは表示側で行う（仕様 3.1）。
struct SpectrumFrame
{
    int fftSize = 0;
    double sampleRate = 48000.0;
    int averages = 0;

    std::vector<std::complex<float>> transfer; // H1（複素）
    std::vector<float> magnitudeDb;            // H1 の振幅 (dB)
    std::vector<float> phaseDeg;               // H1 の位相（−180..180、生位相）
    std::vector<float> coherence;              // γ²（0..1）
    std::vector<float> refPowerDb;             // Ref の自己パワー (dB)（S/N表示用）
    std::vector<float> measPowerDb;            // Meas の自己パワー (dB)
};

class TransferFunctionAccumulator
{
public:
    TransferFunctionAccumulator();

    /** FFTサイズ（点数）・サンプルレート・平均設定・窓を設定する。
        構造に関わる変更（サイズ/レート/窓）があったときだけ平均をリセットする */
    void configure (int fftSizePoints, double sampleRate, const AveragingConfig& averaging,
                    juce::dsp::WindowingFunction<float>::WindowingMethod windowMethod);

    void resetAverages();

    /** Ref/Meas のペアストリームを入力する。処理できたフレーム数を返す */
    int feedSamples (const float* refSamples, const float* measSamples, int numSamples);

    /** 最新の結果を resultOut へコピーする */
    void copyResult (SpectrumFrame& resultOut) const;

    int getAverages() const;

private:
    void processFrame();
    void rebuildWindowData (juce::dsp::WindowingFunction<float>::WindowingMethod method);
    void updateResult();
    void resetAveragesInternal();
    [[nodiscard]] double computeWeight() const;

    mutable juce::CriticalSection lock;

    bool configured = false; // 初回configureを確実に初期化扱いにするためのフラグ

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> windowData;
    juce::dsp::WindowingFunction<float>::WindowingMethod currentWindowMethod =
        juce::dsp::WindowingFunction<float>::hann;

    int fftSize = 8192;
    double sampleRate = 48000.0;
    AveragingConfig config;

    // 直近 fftSize サンプルを組み立てる循環バッファ（各チャンネル）
    std::vector<float> assemblyRef, assemblyMeas;
    int assemblyPos = 0;
    long long samplesSinceFrame = 0;
    long long totalFed = 0;

    // Welch 累積（bin 0..N/2）
    std::vector<double> gxx, gyy;
    std::vector<std::complex<double>> gxy;
    long long averages = 0;
    std::atomic<int> publishedAverages { 0 };

    // FFT 作業バッファ（実FFTは 2N floats を使用）
    std::vector<float> fftBufRef, fftBufMeas;

    SpectrumFrame result;
};

} // namespace eeq::dsp
