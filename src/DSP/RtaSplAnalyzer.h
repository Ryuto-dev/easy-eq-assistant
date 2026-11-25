#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <deque>
#include <vector>

namespace eeq::dsp
{

// Meas 入力1chだけで動く RTA と SPL（仕様 3.4 / 5.4）。
// - RTA: 1/3・1/6・1/12 オクターブバンド、ピークホールド、指数平均（平均時間を設定）
// - SPL: A / C / Z 重み付け、Fast / Slow の時定数、Leq（直近1分・15分・開始からの積算）
// - 校正オフセット（3.4 の比較校正で求めた値）を加算して表示する
//
// 解析スレッドから process() を、UIスレッドから copy系を呼ぶ。内部はロックで保護する
//（フレームは約43msごとなのでロックのコストは無視できる）。
class RtaSplAnalyzer
{
public:
    enum class Weighting { aWeighted, cWeighted, zWeighted };
    enum class TimeConstant { fast, slow }; // Fast=125ms / Slow=1s（仕様 3.4）

    // ---- 設定（UIスレッドから）----

    void configure (double sampleRate, int bandsPerOctave, double rtaAveragingSeconds);
    void setWeighting (Weighting weighting);
    void setTimeConstant (TimeConstant timeConstant);
    void setCalibrationOffsetDb (double offsetDb) noexcept { calibrationOffsetDb = offsetDb; }
    double getCalibrationOffsetDb() const noexcept { return calibrationOffsetDb; }

    // ---- 解析（解析スレッドから）----

    void process (const float* samples, int numSamples);

    // ---- 結果（UIスレッドから）----

    struct RtaFrame
    {
        double sampleRate = 48000.0;
        std::vector<double> centerFreq;
        std::vector<float> levelDb;    // 平滑化後のバンドレベル（校正オフセット込み）
        std::vector<float> peakHoldDb; // ピークホールド
    };

    void copyRta (RtaFrame& out) const;
    void resetPeakHold();

    struct SplSnapshot
    {
        double currentDb = -300.0;   // Fast/Slow 切替の現在値
        double leq1minDb = -300.0;   // 直近1分の Leq
        double leq15minDb = -300.0;  // 直近15分の Leq
        double leqSessionDb = -300.0; // 開始からの積算 Leq
        double peakDb = -300.0;      // 開始からのピーク（時間領域の最大値）
        double sessionSeconds = 0.0;
        bool hasSignal = false;      // まだ有効なフレームが来ていない
    };

    void copySpl (SplSnapshot& out) const;
    void resetSpl();

    // 単体テスト用: IEC 61672 の A/C 重み付けの定義式（1 kHz で 0 dB に正規化）
    [[nodiscard]] static double aWeightingDb (double frequency);
    [[nodiscard]] static double cWeightingDb (double frequency);

    static constexpr int fftOrder = 13; // 8192点。20Hz帯にも複数binが入る分解能

private:
    void rebuildBands();
    void processFrame(); // 窓済み1フレームを処理

    mutable juce::CriticalSection lock;

    bool configured = false;
    double sampleRate = 48000.0;
    int bandsPerOctave = 3;
    double rtaAveragingSeconds = 1.0;
    Weighting weighting = Weighting::aWeighted;
    TimeConstant timeConstant = TimeConstant::fast;
    double calibrationOffsetDb = 0.0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> assembly; // 直近 fftSize サンプル
    int assemblyPos = 0;
    int sinceFrame = 0;
    bool firstFrameDone = false;

    // RTA
    std::vector<double> bandCenters;
    std::vector<double> bandEdgesLo, bandEdgesHi;
    std::vector<double> bandEnergy;      // 線形エネルギー（指数平均）
    std::vector<double> bandPeak;        // ピークホールド（線形）
    RtaFrame rtaOut;

    // SPL
    double fastEnergy = 0.0;   // 指数平滑されたエネルギー
    double slowEnergy = 0.0;
    double sessionEnergyTime = 0.0; // Σ E·dt（A重み。Z重みは積算しない）
    struct EnergySlice { double time = 0.0; double energy = 0.0; double duration = 0.0; };
    std::deque<EnergySlice> slices; // Leq のスライディング窓用
    double sessionSeconds = 0.0;
    double peakSample = 0.0;
    bool hasSignal = false;
};

} // namespace eeq::dsp
