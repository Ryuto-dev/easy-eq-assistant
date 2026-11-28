#pragma once

#include "TransferFunctionAccumulator.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace eeq::dsp
{

// スイープ測定（2026-11）。
// 以前はスイープもピンクノイズと同じ「直近 N フレームの Welch 平均」に流していたが、スイープは
// その瞬間に 1 つの周波数しか鳴っていないので、鳴り終わった帯域から平均の窓の外へ抜けていき、
// コヒーレンスが下がってデータが消えていった。スイープは「鳴らし終わってから全体を一度に割る」のが正しい:
//   H(f) = Meas(f)·conj(Ref(f)) / (|Ref(f)|² + β)     （Ref は実際に取り込んだ Ref ch）
// 逆 FFT した長い IR から、伝達関数（ユーザーの FFT サイズ）と残響時間の解析をする。
// 対数スイープの高調波歪みは IR の負の時間（バッファの末尾）に出るので、伝達関数には混ざらない。
struct SweepImpulse
{
    double sampleRate = 48000.0;
    std::vector<float> ir; // 長さ N（2 の冪、循環）。index 0 が「Ref と同時」
    int peakIndex = 0;     // 直接音のピーク（正の時間側で最大の |h|）
    int recordedLength = 0; // 録音の長さ
    int sweepStart = 0, sweepLength = 0; // 録音の中でスイープが鳴っていた区間（Ref から検出）
    double peakValue = 0.0;
    std::vector<float> refPowerDb, measPowerDb; // 長さ N/2+1 の生スペクトル（dB）

    bool isValid() const noexcept { return ir.size() >= 1024; }
};

namespace sweepdetail
{
    inline int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }
    inline int orderOf (int n) { int o = 0; while ((1 << o) < n) ++o; return o; }

    /** FFT の準備は 2^20 点で 0.5 秒以上かかる（実行は数 ms）ので、次数ごとに作って使い回す */
    inline juce::dsp::FFT& fftFor (int order)
    {
        static std::mutex mutex;
        static std::map<int, std::unique_ptr<juce::dsp::FFT>> cache;
        const std::lock_guard<std::mutex> lock (mutex);
        auto& slot = cache[order];
        if (slot == nullptr)
            slot = std::make_unique<juce::dsp::FFT> (order);
        return *slot;
    }

    /** 実信号の FFT（juce::dsp::FFT の実数用の並びをそのまま複素スペクトルに直す） */
    inline std::vector<std::complex<float>> forward (const std::vector<float>& x, int n)
    {
        auto& fft = fftFor (orderOf (n));
        std::vector<float> buffer ((size_t) n * 2, 0.0f);
        std::copy_n (x.begin(), std::min ((int) x.size(), n), buffer.begin());
        fft.performRealOnlyForwardTransform (buffer.data(), true);
        std::vector<std::complex<float>> spectrum ((size_t) n / 2 + 1);
        for (size_t k = 0; k < spectrum.size(); ++k)
            spectrum[k] = { buffer[2 * k], buffer[2 * k + 1] };
        return spectrum;
    }

    inline std::vector<float> inverse (const std::vector<std::complex<float>>& spectrum, int n)
    {
        auto& fft = fftFor (orderOf (n));
        std::vector<float> buffer ((size_t) n * 2, 0.0f);
        for (size_t k = 0; k < spectrum.size(); ++k)
        {
            buffer[2 * k] = spectrum[k].real();
            buffer[2 * k + 1] = spectrum[k].imag();
        }
        // 負の周波数側（実信号なので共役）も埋める
        for (int k = 1; k < n / 2; ++k)
        {
            buffer[2 * (size_t) (n - k)] = spectrum[(size_t) k].real();
            buffer[2 * (size_t) (n - k) + 1] = -spectrum[(size_t) k].imag();
        }
        fft.performRealOnlyInverseTransform (buffer.data());
        buffer.resize ((size_t) n);
        return buffer; // JUCE の逆変換は 1/N 込み
    }
}

/** 取り込んだ Ref / Meas（同じ長さ）からインパルス応答を求める */
inline bool deconvolveSweep (const std::vector<float>& ref, const std::vector<float>& meas, double sampleRate,
                             SweepImpulse& out)
{
    using namespace sweepdetail;
    const auto length = (int) std::min (ref.size(), meas.size());

    if (length < 4096 || ! (sampleRate > 0.0))
        return false;

    // 循環の折り返しで残響と高調波が重ならないよう、録音長の 1.5 倍以上の 2 の冪にする
    auto n = nextPow2 (length);
    if (n < length + length / 2)
        n *= 2;

    const auto R = forward (ref, n);
    const auto M = forward (meas, n);

    double maxRef = 0.0;
    for (const auto& r : R)
        maxRef = std::max (maxRef, (double) std::norm (r));

    if (! (maxRef > 0.0))
        return false;

    // 正則化: Ref が −60 dB より弱い bin（スイープの帯域外）は H を 0 に寄せる
    const auto beta = maxRef * 1.0e-6;
    std::vector<std::complex<float>> H (R.size());
    out.refPowerDb.assign (R.size(), -200.0f);
    out.measPowerDb.assign (R.size(), -200.0f);

    for (size_t k = 0; k < R.size(); ++k)
    {
        const auto r = std::complex<double> (R[k]);
        const auto m = std::complex<double> (M[k]);
        const auto rr = std::norm (r);
        const auto h = m * std::conj (r) / (rr + beta);
        H[k] = { (float) h.real(), (float) h.imag() };
        out.refPowerDb[k] = (float) (10.0 * std::log10 (std::max (rr / n, 1.0e-20)));
        out.measPowerDb[k] = (float) (10.0 * std::log10 (std::max (std::norm (m) / n, 1.0e-20)));
    }
    H[0] = 0.0f;

    out.ir = inverse (H, n);
    out.sampleRate = sampleRate;
    out.recordedLength = length;

    // Ref の中でスイープが鳴っていた区間（雑音区間を決めるのに使う）
    float refPeak = 0.0f;
    for (int i = 0; i < length; ++i)
        refPeak = std::max (refPeak, std::abs (ref[(size_t) i]));
    int first = 0, last = length - 1;
    while (first < length && std::abs (ref[(size_t) first]) < refPeak * 1.0e-3f) ++first;
    while (last > first && std::abs (ref[(size_t) last]) < refPeak * 1.0e-3f) --last;
    out.sweepStart = first;
    out.sweepLength = juce::jmax (1, last - first + 1);

    // 直接音: 正の時間側（前半）で最大の |h|
    out.peakIndex = 0;
    out.peakValue = 0.0;
    for (int i = 0; i < n / 2; ++i)
        if (const auto v = std::abs ((double) out.ir[(size_t) i]); v > out.peakValue)
        {
            out.peakValue = v;
            out.peakIndex = i;
        }

    return out.peakValue > 0.0;
}

/** 帯域（上端 upperHz まで）の雑音区間。
    対数スイープの割り算では、録音の時刻 τ の雑音は周波数 f について IR の時刻 τ − (スイープ開始 + t_f) に出る
    （t_f はスイープが f を鳴らした時刻）。高い周波数ほど後半に鳴るので、正の時間側に雑音が出るのは
    「録音の終わり − スイープ開始 − t_f」まで。その手前の 30 % を雑音の基準にする（残響はそれより前に消える前提。
    鳴り終わってから 2 秒待つのはこのため）。20 Hz〜20 kHz の対数スイープを前提にする */
inline juce::Range<int> sweepNoiseWindow (const SweepImpulse& impulse, double upperHz = 20000.0)
{
    const auto sweepSamples = (double) impulse.sweepLength;
    const auto tf = sweepSamples * std::log (juce::jlimit (20.0, 20000.0, upperHz) / 20.0) / std::log (1000.0);
    auto end = (int) ((double) (impulse.recordedLength - impulse.sweepStart) - tf);
    end = juce::jmin (end, (int) impulse.ir.size() / 2);
    const auto span = juce::jmax (0, end - impulse.peakIndex);
    return { impulse.peakIndex + span * 6 / 10, impulse.peakIndex + span * 95 / 100 };
}

/** IR の一部（start から fftSize 点。start より前の pre 点は末尾に回す）を FFT して伝達関数にする */
namespace sweepdetail
{
    inline std::vector<std::complex<float>> segmentSpectrum (const std::vector<float>& ir, int start, int fftSize)
    {
        const auto n = (int) ir.size();
        const auto pre = fftSize / 16;
        const auto post = fftSize - pre;
        std::vector<float> segment ((size_t) fftSize, 0.0f);
        const auto fadeIn = juce::jmax (1, pre / 2), fadeOut = juce::jmax (1, post / 8);

        for (int i = 0; i < post; ++i)
        {
            auto w = 1.0f;
            if (i >= post - fadeOut)
                w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - (post - fadeOut)) / (float) fadeOut);
            segment[(size_t) i] = ir[(size_t) (((start + i) % n + n) % n)] * w;
        }

        for (int i = 0; i < pre; ++i)
        {
            auto w = 1.0f;
            if (i < fadeIn)
                w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) fadeIn);
            segment[(size_t) (post + i)] = ir[(size_t) (((start - pre + i) % n + n) % n)] * w;
        }

        return forward (segment, fftSize);
    }
}

/** スイープの IR から、ユーザーの FFT サイズの伝達関数フレームを作る。
    refDelaySamples: 時間合わせ（Ref を遅らせる量）。ノイズ測定と同じく、その分を位相から除いたフレームになる。
    コヒーレンスの代わりに、同じ長さの雑音区間（IR の中ほど）との比から γ² = S/(S+N) を入れる */
inline void frameFromSweep (const SweepImpulse& impulse, int fftSize, int refDelaySamples, SpectrumFrame& out)
{
    using namespace sweepdetail;
    const auto n = (int) impulse.ir.size();
    fftSize = juce::jlimit (256, juce::jmax (256, n / 2), nextPow2 (fftSize));

    const auto signal = segmentSpectrum (impulse.ir, refDelaySamples, fftSize);
    const auto noiseWindow = sweepNoiseWindow (impulse);
    const auto noise = segmentSpectrum (impulse.ir, juce::jmax (noiseWindow.getStart(), noiseWindow.getEnd() - fftSize), fftSize);
    const auto bins = (size_t) fftSize / 2 + 1;

    out = SpectrumFrame();
    out.fftSize = fftSize;
    out.sampleRate = impulse.sampleRate;
    out.averages = 2; // スイープ 1 回で確定（コヒーレンスは S/N から入れているので信頼できる扱い）
    out.refDelaySamples = (double) refDelaySamples;
    out.transfer.resize (bins);
    out.magnitudeDb.resize (bins);
    out.phaseDeg.resize (bins);
    out.coherence.resize (bins);
    out.refPowerDb.resize (bins);
    out.measPowerDb.resize (bins);

    // S/N は 1/12 oct で平滑化してから比べる（1 bin ごとだと雑音が暴れる）
    std::vector<double> s (bins), nz (bins), cs (bins + 1, 0.0), cn (bins + 1, 0.0);
    for (size_t k = 0; k < bins; ++k)
    {
        s[k] = std::norm (std::complex<double> (signal[k]));
        nz[k] = std::norm (std::complex<double> (noise[k]));
        cs[k + 1] = cs[k] + s[k];
        cn[k + 1] = cn[k] + nz[k];
    }

    const auto ratio = (double) (impulse.refPowerDb.size() - 1) / (double) (bins - 1); // 長いスペクトルの bin / このフレームの bin

    for (size_t k = 0; k < bins; ++k)
    {
        out.transfer[k] = signal[k];
        out.magnitudeDb[k] = (float) (10.0 * std::log10 (std::max (s[k], 1.0e-24)));
        out.phaseDeg[k] = (float) (std::arg (std::complex<double> (signal[k])) * 180.0 / juce::MathConstants<double>::pi);

        const auto half = std::max<size_t> (1, (size_t) std::lround ((double) k * 0.029)); // ±1/24 oct
        const auto lo = k > half ? k - half : 0, hi = std::min (bins, k + half + 1);
        const auto sm = (cs[hi] - cs[lo]) / (double) (hi - lo);
        const auto nm = (cn[hi] - cn[lo]) / (double) (hi - lo);
        out.coherence[k] = k == 0 ? 0.0f : (float) juce::jlimit (0.0, 1.0, sm / (sm + nm + 1.0e-30));

        // Ref / Meas のパワーは長いスペクトルの該当範囲の平均（S/N 表示と IR の Ref 床判定に使う）
        const auto c = (size_t) std::lround ((double) k * ratio);
        const auto span = std::max<size_t> (1, (size_t) (ratio * 0.5));
        const auto a = c > span ? c - span : 0, b = std::min (impulse.refPowerDb.size(), c + span + 1);
        double refSum = 0.0, measSum = 0.0;
        for (auto i = a; i < b; ++i)
        {
            refSum += std::pow (10.0, impulse.refPowerDb[i] / 10.0);
            measSum += std::pow (10.0, impulse.measPowerDb[i] / 10.0);
        }
        out.refPowerDb[k] = (float) (10.0 * std::log10 (std::max (refSum / (double) (b - a), 1.0e-20)));
        out.measPowerDb[k] = (float) (10.0 * std::log10 (std::max (measSum / (double) (b - a), 1.0e-20)));
    }
}

// ---- 残響時間（ISO 3382-1 / -2 のやり方を簡略化）----
// オクターブ帯域ごとに IR を周波数領域で帯域通過させ、Schroeder の逆積分で減衰曲線（EDC）を作る。
// 雑音床は IR の雑音区間から取り、減衰曲線が雑音床 +5 dB に交わる点で打ち切る（Lundeby の簡略版）。
// EDT: 0〜−10 dB、T20: −5〜−25 dB、T30: −5〜−35 dB の直線当てはめを 60 dB に伸ばした値。
struct ReverbBand
{
    double centreHz = 0.0;
    double edt = 0.0, t20 = 0.0, t30 = 0.0; // 秒（0 は測れなかった）
    double dynamicRangeDb = 0.0;            // ピークと雑音床の差（T20 は 35 dB、T30 は 45 dB 以上ほしい）
    std::vector<float> edcDb;               // 減衰曲線（1 ms ごと、0 dB 始まり）
    std::vector<float> envelopeDb;          // IR の包絡（1 ms ごとのエネルギー、ピーク 0 dB）

    // 明瞭度（ISO 3382-1。直接音から数えたエネルギーの比。雑音を差し引き、雑音床に落ちたところまで積分）
    bool hasClarity = false;                // S/N 30 dB 未満では出さない（雑音で値が偏る）
    double c50 = 0.0, c80 = 0.0;            // dB: 50 / 80 ms までと、それ以降の比
    double d50 = 0.0;                       // 0..1: 50 ms までが全体に占める割合
    double centreTimeMs = 0.0;              // Ts: エネルギーの重心時刻
    double mti = -1.0;                      // STI 用の変調伝達指数（帯域ごと。-1 は未計算）
};

struct ReverbResult
{
    // STI（IEC 60268-16 rev.4 の男性話者の重み。IR から求める簡易法: 背景雑音・マスキング・聴覚閾値の補正なし。
    // 実際の会場では客の雑音で下がる）。125 Hz〜8 kHz の全帯域が測れたときだけ（-1 は未計算）
    double sti = -1.0;
    std::vector<ReverbBand> bands; // 63 Hz〜8 kHz のオクターブ＋最後に広帯域
    double stepSeconds = 0.001;
};

namespace sweepdetail
{
    inline bool fitDecay (const std::vector<double>& edcDb, double stepSeconds, double fromDb, double toDb, double& rt)
    {
        // fromDb を最初に下回る点から toDb を最初に下回る点までを最小二乗で当てはめる
        int i0 = -1, i1 = -1;
        for (int i = 0; i < (int) edcDb.size(); ++i)
        {
            if (i0 < 0 && edcDb[(size_t) i] <= fromDb) i0 = i;
            if (i0 >= 0 && edcDb[(size_t) i] <= toDb) { i1 = i; break; }
        }

        if (i0 < 0 || i1 < 0 || i1 - i0 < 3)
            return false;

        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const auto count = (double) (i1 - i0 + 1);
        for (int i = i0; i <= i1; ++i)
        {
            const auto x = i * stepSeconds, y = edcDb[(size_t) i];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const auto slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);

        if (! (slope < -1.0e-3))
            return false;

        rt = -60.0 / slope;
        return std::isfinite (rt) && rt > 0.0 && rt < 30.0;
    }

    inline ReverbBand analyseBand (const std::vector<float>& bandIr, int peakSearchEnd, int noiseStart, int noiseEnd,
                                   double sampleRate, double centreHz)
    {
        ReverbBand band;
        band.centreHz = centreHz;
        // 1 ms ごと（小さい部屋や近いマイクでは 20 dB が数十 ms で落ちるので、10 ms では当てはめの点が足りない）
        const auto step = juce::jmax (1, (int) std::lround (sampleRate * 0.001));

        // 直接音（この帯域で最大のエネルギー）
        int peak = 0;
        double peakEnergy = 0.0;
        for (int i = 0; i < peakSearchEnd; ++i)
            if (const auto e = (double) bandIr[(size_t) i] * bandIr[(size_t) i]; e > peakEnergy)
            {
                peakEnergy = e;
                peak = i;
            }

        if (! (peakEnergy > 0.0))
            return band;

        double noise = 0.0;
        for (int i = noiseStart; i < noiseEnd; ++i)
            noise += (double) bandIr[(size_t) i] * bandIr[(size_t) i];
        noise /= (double) juce::jmax (1, noiseEnd - noiseStart);

        // 1 ms ごとのエネルギー包絡（ピークの少し前から雑音区間の手前まで）
        const auto start = juce::jmax (0, peak - step / 4);
        const auto end = juce::jmin (noiseStart, peak + (int) (sampleRate * 8.0));
        std::vector<double> blocks;
        for (int i = start; i + step <= end; i += step)
        {
            double e = 0.0;
            for (int j = i; j < i + step; ++j)
                e += (double) bandIr[(size_t) j] * bandIr[(size_t) j];
            blocks.push_back (e / step);
        }

        if (blocks.size() < 5)
            return band;

        const auto blockPeak = *std::max_element (blocks.begin(), blocks.end());
        band.dynamicRangeDb = 10.0 * std::log10 (blockPeak / std::max (noise, 1.0e-30));

        // 雑音床 +5 dB に落ちたところで打ち切る（10 ms の移動平均で判定し、50 ms 先まで雑音のままかも確かめる）
        std::vector<double> smooth (blocks.size(), 0.0);
        {
            double acc = 0.0;
            for (size_t i = 0; i < blocks.size(); ++i)
            {
                acc += blocks[i];
                if (i >= 10) acc -= blocks[i - 10];
                smooth[i] = acc / (double) std::min<size_t> (i + 1, 10);
            }
        }
        auto cross = (int) blocks.size();
        for (int i = 10; i < (int) blocks.size(); ++i)
            if (smooth[(size_t) i] < noise * 3.16)
            {
                bool stays = true;
                for (int j = i; j < std::min ((int) blocks.size(), i + 50); j += 10)
                    stays = stays && smooth[(size_t) j] < noise * 6.0;
                if (stays) { cross = i; break; }
            }

        // Schroeder 逆積分（雑音のエネルギーを差し引いてから）
        std::vector<double> edc ((size_t) cross, 0.0);
        double acc = 0.0;
        for (int i = cross - 1; i >= 0; --i)
        {
            acc += std::max (0.0, blocks[(size_t) i] - noise);
            edc[(size_t) i] = acc;
        }

        if (! (edc.front() > 0.0))
            return band;

        std::vector<double> edcDb (edc.size());
        for (size_t i = 0; i < edc.size(); ++i)
            edcDb[i] = 10.0 * std::log10 (std::max (edc[i] / edc.front(), 1.0e-12));

        for (auto v : edcDb)
            band.edcDb.push_back ((float) v);
        for (auto v : blocks)
            band.envelopeDb.push_back ((float) (10.0 * std::log10 (std::max (v / blockPeak, 1.0e-12))));

        const auto stepSeconds = (double) step / sampleRate;

        // 明瞭度と STI 用の変調伝達（雑音を差し引いたエネルギーで、雑音床に落ちたところまで）
        if (band.dynamicRangeDb >= 30.0)
        {
            const auto at50 = (int) std::lround (0.050 / stepSeconds), at80 = (int) std::lround (0.080 / stepSeconds);
            double early50 = 0.0, early80 = 0.0, total = 0.0, moment = 0.0;
            for (int i = 0; i < cross; ++i)
            {
                const auto e = std::max (0.0, blocks[(size_t) i] - noise);
                const auto t = ((double) i + 0.5) * stepSeconds;
                total += e;
                moment += e * t;
                if (i < at50) early50 += e;
                if (i < at80) early80 += e;
            }

            if (total > 0.0)
            {
                // 後部が雑音より小さい（近いマイクで全部 50 ms 以内に収まる）ときは 60 dB で頭打ちにする
                const auto floorEnergy = total * 1.0e-6;
                band.hasClarity = true;
                band.c50 = 10.0 * std::log10 (early50 / std::max (total - early50, floorEnergy));
                band.c80 = 10.0 * std::log10 (early80 / std::max (total - early80, floorEnergy));
                band.d50 = early50 / total;
                band.centreTimeMs = moment / total * 1000.0;

                // 変調伝達関数 m(F) = |Σ e(t)·exp(−j2πFt)| / Σ e(t)（Schroeder）。14 の変調周波数で TI を平均する
                const double modulation[] = { 0.63, 0.8, 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0, 10.0, 12.5 };
                double tiSum = 0.0;
                for (const auto F : modulation)
                {
                    std::complex<double> sum = 0.0;
                    for (int i = 0; i < cross; ++i)
                    {
                        const auto e = std::max (0.0, blocks[(size_t) i] - noise);
                        sum += e * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * F * ((double) i + 0.5) * stepSeconds);
                    }
                    const auto m = juce::jlimit (1.0e-6, 1.0 - 1.0e-6, std::abs (sum) / total);
                    const auto snr = juce::jlimit (-15.0, 15.0, 10.0 * std::log10 (m / (1.0 - m)));
                    tiSum += (snr + 15.0) / 30.0;
                }
                band.mti = tiSum / 14.0;
            }
        }

        double value = 0.0;
        if (fitDecay (edcDb, stepSeconds, -0.01, -10.0, value)) band.edt = value;
        if (band.dynamicRangeDb >= 35.0 && fitDecay (edcDb, stepSeconds, -5.0, -25.0, value)) band.t20 = value;
        if (band.dynamicRangeDb >= 45.0 && fitDecay (edcDb, stepSeconds, -5.0, -35.0, value)) band.t30 = value;
        return band;
    }
}

/** 帯域ごとの MTI から STI（IEC 60268-16 rev.4、男性話者の α/β。125 Hz〜8 kHz の 7 帯域）。足りなければ -1 */
inline double speechTransmissionIndex (const ReverbResult& result)
{
    const double centres[] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
    const double alpha[] = { 0.085, 0.127, 0.230, 0.233, 0.309, 0.224, 0.173 };
    const double beta[] = { 0.085, 0.078, 0.065, 0.011, 0.047, 0.095 };
    double mti[7];

    for (int k = 0; k < 7; ++k)
    {
        mti[k] = -1.0;
        for (const auto& band : result.bands)
            if (std::abs (band.centreHz - centres[k]) < 1.0)
                mti[k] = band.mti;
        if (mti[k] < 0.0)
            return -1.0;
    }

    double sti = 0.0;
    for (int k = 0; k < 7; ++k)
        sti += alpha[k] * mti[k];
    for (int k = 0; k < 6; ++k)
        sti -= beta[k] * std::sqrt (mti[k] * mti[k + 1]);
    return juce::jlimit (0.0, 1.0, sti);
}

inline ReverbResult analyseReverb (const SweepImpulse& impulse)
{
    using namespace sweepdetail;
    ReverbResult result;

    if (! impulse.isValid())
        return result;

    const auto n = (int) impulse.ir.size();
    const auto fs = impulse.sampleRate;
    const auto spectrum = forward (impulse.ir, n);

    // 雑音区間は帯域ごと（高い帯域ほどスイープの後半で鳴るので、雑音の出る範囲が短い）
    const auto broadbandNoise = sweepNoiseWindow (impulse);
    const auto peakSearchEnd = juce::jmin (broadbandNoise.getStart(), impulse.peakIndex + (int) (fs * 0.05));

    const double centres[] = { 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };

    for (const auto fc : centres)
    {
        if (fc * 1.41 > fs * 0.45)
            break;

        // オクターブ帯域（±1/2 oct）に、境目 1/6 oct の余弦の肩をつけた零位相のフィルタ
        auto filtered = spectrum;
        for (size_t k = 0; k < filtered.size(); ++k)
        {
            const auto f = (double) k * fs / (double) n;
            double g = 0.0;
            if (f > 0.0)
            {
                const auto d = std::abs (std::log2 (f / fc)); // 中心からのオクターブ距離
                g = d <= 0.5 - 1.0 / 12.0 ? 1.0
                  : d >= 0.5 + 1.0 / 12.0 ? 0.0
                  : 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (d - (0.5 - 1.0 / 12.0)) * 6.0);
            }
            filtered[k] *= (float) g;
        }

        const auto bandIr = inverse (filtered, n);
        const auto noise = sweepNoiseWindow (impulse, fc * 1.6);
        result.bands.push_back (analyseBand (bandIr, juce::jmax (1, peakSearchEnd), noise.getStart(), noise.getEnd(), fs, fc));
    }

    // 広帯域
    result.bands.push_back (analyseBand (impulse.ir, juce::jmax (1, peakSearchEnd), broadbandNoise.getStart(), broadbandNoise.getEnd(), fs, 0.0));
    result.sti = speechTransmissionIndex (result);
    return result;
}

} // namespace eeq::dsp
