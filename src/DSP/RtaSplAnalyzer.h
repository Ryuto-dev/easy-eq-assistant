#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace eeq::dsp
{

// Meas 入力1chだけで動く RTA と SPL（仕様 3.4 / 5.4）。
// - RTA: 1/3・1/6・1/12 オクターブバンド、ピークホールド、指数平均（平均時間を設定）
// - SPL: A / C / Z 重み付け、Fast / Slow の時定数、Leq（直近1分・15分・開始からの積算）
//   SPL は FFT 経路とは独立に時間領域で計算する（IEC 61672: 重み付け IIR → 二乗 →
//   サンプルごとの指数平滑。#111）
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

    /** 表示用（本番モードの「dB(A) Fast」表記など: issue #139） */
    Weighting getWeighting() const { const juce::ScopedLock sl (lock); return weighting; }
    TimeConstant getTimeConstant() const { const juce::ScopedLock sl (lock); return timeConstant; }
    void setCalibrationOffsetDb (double offsetDb) noexcept { calibrationOffsetDb.store (offsetDb, std::memory_order_relaxed); }
    double getCalibrationOffsetDb() const noexcept { return calibrationOffsetDb.load (std::memory_order_relaxed); }

    // ---- 解析（解析スレッドから）----

    void process (const float* samples, int numSamples);

    // ---- 結果（UIスレッドから）----

    struct RtaFrame
    {
        double sampleRate = 48000.0;
        std::vector<double> centerFreq;  // 正確な中心周波数（IEC 61260: 1000·10^(0.3n/b)）
        std::vector<double> nominalFreq; // 表示用の公称周波数（1k, 1.25k, …）
        std::vector<float> levelDb;    // 平滑化後のバンドレベル（校正オフセット込み）
        std::vector<float> peakHoldDb; // ピークホールド
    };

    void copyRta (RtaFrame& out) const;
    /** UIタイマー用。解析中なら待たずに false を返し、outは変更しない */
    bool tryCopyRta (RtaFrame& out) const;
    void resetPeakHold();

    struct SplSnapshot
    {
        double currentDb = -300.0;   // Fast/Slow 切替の現在値
        double leq1minDb = -300.0;   // 直近1分の Leq
        double leq15minDb = -300.0;  // 直近15分の Leq
        double leqSessionDb = -300.0; // 開始からの積算 Leq
        double peakDb = -300.0;      // 開始からのピーク Lpeak（Z特性。正弦波の RMS 基準なので |x|·√2 で換算）
        double sessionSeconds = 0.0;
        bool hasSignal = false;      // まだ有効なフレームが来ていない
    };

    void copySpl (SplSnapshot& out) const;
    /** UIタイマー用。解析中なら待たずに false を返し、outは変更しない */
    bool tryCopySpl (SplSnapshot& out) const;
    void resetSpl();

    /** RTAとSPLを1回のロックで取得する（UIタイマー用。ロック回数を減らす: issue #37） */
    void copyAll (RtaFrame& rtaFrameOut, SplSnapshot& splOut) const;
    /** UIタイマー用。解析中なら待たずに false を返し、出力は変更しない */
    bool tryCopyAll (RtaFrame& rtaFrameOut, SplSnapshot& splOut) const;

    // 単体テスト用: IEC 61672 の A/C 重み付けの定義式（1 kHz で 0 dB に正規化）
    [[nodiscard]] static double aWeightingDb (double frequency);
    [[nodiscard]] static double cWeightingDb (double frequency);

    /** 比較校正のオフセットを求める（issue #48）。
        displayedDb は「現在のオフセット込み」の表示値なので、いったん生の値に戻して
        から差を取る。これがないと 2 回目の校正で offset_new = K − offset_old となり、
        オフセットが 0 に戻ってしまう */
    [[nodiscard]] static double calibrationOffsetFor (double meterReadingDb, double displayedDb,
                                                      double currentOffsetDb) noexcept
    {
        return meterReadingDb - (displayedDb - currentOffsetDb);
    }

    /** RTA 表示スケールの上端 [dB]（issue #49）。
        校正オフセットは「0 dBFS が何 dB SPL に相当するか」の値なので、校正後の
        上端は offset そのもの。（94 dB はキャリブレータの値であって 0 dBFS の SPL
        ではない。オフセットにはすでに含まれている） */
    [[nodiscard]] static float rtaScaleTopDb (double calibrationOffsetDb) noexcept
    {
        return std::abs (calibrationOffsetDb) < 1.0e-9 ? 0.0f : (float) calibrationOffsetDb;
    }

    /** 単体テスト用: SPL 経路の重み付け IIR の振幅特性 [dB]（1 kHz で 0 dB） */
    [[nodiscard]] static double weightingFilterResponseDb (Weighting weighting, double frequency, double sampleRate);

    /** IEC 61260 の厳密な中心周波数 fc = 1000·10^(0.3·n/b) */
    [[nodiscard]] static double exactBandCenter (int n, int bandsPerOctave) noexcept
    {
        return 1000.0 * std::pow (10.0, 0.3 * (double) n / (double) bandsPerOctave);
    }

    /** 表示用の公称中心周波数（1/3 oct 系列に乗る帯域は R10 系列の値、それ以外は有効数字3桁） */
    [[nodiscard]] static double nominalBandFrequency (double exactCenter, int bandsPerOctave);

    static constexpr int fftOrder = 13; // 8192点。20Hz帯にも複数binが入る分解能

private:
    void rebuildBands();
    /** copySpl のロック外実体（copySpl / copyAll から使う。呼び出し側でロックを取ること） */
    void copySplUnlocked (SplSnapshot& out) const;
    void processFrame(); // 窓済み1フレームを処理
    void resetSplUnlocked (bool keepPeak);
    void processSplSample (double sample);

    // 時間領域の重み付けフィルタ（双二次のカスケード。転置直接形 II）
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        double process (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct WeightingFilter
    {
        std::array<Biquad, 3> sections {};
        int numSections = 0;
        double gain = 1.0;

        void design (Weighting type, double fs);
        void reset() noexcept
        {
            for (auto& section : sections)
                section.z1 = section.z2 = 0.0;
        }
        double process (double x) noexcept
        {
            auto y = x * gain;

            for (int i = 0; i < numSections; ++i)
                y = sections[(size_t) i].process (y);

            return y;
        }
    };

    mutable juce::CriticalSection lock;

    bool configured = false;
    double sampleRate = 48000.0;
    int bandsPerOctave = 3;
    double rtaAveragingSeconds = 1.0;
    Weighting weighting = Weighting::aWeighted;
    TimeConstant timeConstant = TimeConstant::fast;
    std::atomic<double> calibrationOffsetDb { 0.0 };

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::vector<float> assembly; // 直近 fftSize サンプル
    int assemblyPos = 0;
    int sinceFrame = 0;
    bool firstFrameDone = false;

    // processFrame の作業バッファ（メンバーで持って毎フレームの確保を避ける）
    std::vector<float> frameBuffer;
    std::vector<double> binPower;

    // RTA
    std::vector<double> bandCenters;
    std::vector<double> bandEdgesLo, bandEdgesHi;
    std::vector<double> bandEnergy;      // 線形エネルギー（指数平均）
    std::vector<double> bandPeak;        // ピークホールド（線形）
    double sineRefPowerDb = 0.0;         // フルスケール正弦波の全正周波数パワー（dBFS基準）
    RtaFrame rtaOut;

    // SPL（時間領域。エネルギーは重み付け後の二乗平均で、フルスケール正弦波 = 0.5）
    WeightingFilter weightingFilter;
    double fastAlpha = 0.0, slowAlpha = 0.0; // サンプルごとの指数平滑係数
    double fastEnergy = 0.0;   // 指数平滑された二乗値
    double slowEnergy = 0.0;
    double pendingEnergy = 0.0; // まだスライスにしていない Σ y²
    int pendingSamples = 0;
    double sessionEnergyTime = 0.0; // Σ y²·dt（現在の重み付け。切替時はリセット: #119）
    struct EnergySlice { double time = 0.0; double energy = 0.0; double duration = 0.0; };
    std::deque<EnergySlice> slices; // Leq のスライディング窓用
    double sessionSeconds = 0.0;
    double peakSample = 0.0;
    bool hasSignal = false;
};

} // namespace eeq::dsp
