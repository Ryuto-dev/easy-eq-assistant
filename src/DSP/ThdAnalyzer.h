#pragma once

#include "Core/JuceModules.h"
#include "SweepGenerator.h"

#include <array>
#include <cmath>
#include <vector>

namespace eeq::dsp
{

// THD解析（対数スイープの録音から高調波を分離する。issue #30）。
//
// 従来の実装は Welch 平均した H1（循環IR 8192点）から高調波を探していたため、
// 10秒スイープの高調波（到達 1〜2.3 秒 = 4.8万〜11万サンプル先）が常に IR の外に
// 出てしまい、THD が原理的に 0.00% と表示されるだけだった。
//
// Farina 法では (1) スイープ再生中の入出力を連続録音し、(2) 逆フィルタ
// d = IFFT( Y·conj(X) / (|X|² + ε) ) でデコンボリューションすると、
// 線形応答が t = L（I/O遅延）、k 次高調波が t = L − T·ln(k)/ln(f2/f1)
// （負の時間側。循環畳み込みでは末尾へ折り返す）に現れる。各位置の窓RMSから
// THD = sqrt(Σ_{k≥2} h_k²) / h_1 を求める。
//
// #114: 以前は線形 IR と高調波 IR の ±5 ms 窓 RMS の比（= Hz あたりのエネルギーで
// 重み付けした集約）だったため、帯域の広い高域が支配し、低域だけの歪みが見えなかった
// （200 Hz 未満だけ 10% の2次歪みで 1.9% 表示）。現在は各 IR を時間窓で切り出して FFT し、
// 基本波周波数 f0 ごとに THD(f0) = √(Σ|H_k(k·f0)|²) / |H_1(f0)| を 1/3 oct バンドで求める
// （対数スイープでは |H_k(ν)| が出力周波数 ν での k 次高調波の相対振幅になる）。
// 逆フィルタの正則化による高域の減衰（20 kHz で −6 dB）は、既知のゲイン
// |X|²/(|X|²+ε) で割って補正する。
struct ThdBand
{
    double centerHz = 0.0;     // IEC 61260 の厳密な中心周波数（基本波）
    double nominalHz = 0.0;    // 公称値（表示用）
    double thdPercent = 0.0;
    std::array<double, 4> harmonicPercent {}; // 2次〜5次
    double fundamentalDb = -300.0;            // 基本波の相対レベル（バンド間の比較用）
};

struct ThdResult
{
    bool valid = false;
    juce::String failureReason;      // valid=false のときの理由（画面にそのまま出す）
    double thdPercent = 0.0;         // 代表値: 有効バンドのうち THD が最大のバンドの値
    double worstFrequencyHz = 0.0;   // 代表値を取ったバンドの公称周波数
    double linearLagSamples = 0.0;   // 線形応答のピーク位置（I/O遅延込み）
    std::array<double, 4> harmonicPercent {}; // 代表値を取ったバンドの 2次〜5次
    std::vector<ThdBand> bands;      // 1/3 oct バンドごとの THD（基本波周波数の昇順）
};

/** スイープ録音からTHDを解析する。
    refSweep: 録音した基準信号（スイープそのもの。内部参照なら出力信号）
    recorded: 同区間の測定入力
    sweepSeconds/f1/f2: 再生したスイープの条件（LimitTestTab と同じ値を渡すこと） */
[[nodiscard]] inline ThdResult analyzeThdFromSweepRecording (const std::vector<float>& refSweep,
                                                             const std::vector<float>& recorded,
                                                             double sampleRate,
                                                             double sweepSeconds,
                                                             double f1, double f2)
{
    ThdResult result;

    if (refSweep.size() < 1024 || recorded.size() < 1024)
    {
        result.failureReason = juce::String (u8"録音データが不足しています");
        return result;
    }

    const auto sweepSamples = (size_t) std::llround (sweepSeconds * sampleRate);
    const auto tailSamples = (size_t) std::llround (1.0 * sampleRate);

    // スイープ本体+残響分が録音できていないと高調波を分離できない
    if (recorded.size() < sweepSamples + tailSamples)
    {
        result.failureReason = juce::String (u8"録音長が不足しています（サンプルレートが高い場合はTHDを測定できません）");
        return result;
    }

    // デコンボリューション用のFFT長（録音全体を収める2の冪）
    auto fftOrder = 10;

    while ((1 << fftOrder) < (int) recorded.size())
        ++fftOrder;

    const auto n = (size_t) 1 << fftOrder;

    std::vector<float> ref ((size_t) n, 0.0f);
    std::vector<float> meas ((size_t) n, 0.0f);
    std::copy (refSweep.begin(), refSweep.end(), ref.begin());
    std::copy (recorded.begin(), recorded.end(), meas.begin());

    juce::dsp::FFT fft (fftOrder);
    std::vector<std::complex<float>> refSpec ((size_t) n), measSpec ((size_t) n);

    {
        std::vector<std::complex<float>> in ((size_t) n);

        for (size_t i = 0; i < n; ++i)
            in[i] = { ref[i], 0.0f };

        fft.perform (in.data(), refSpec.data(), false);

        for (size_t i = 0; i < n; ++i)
            in[i] = { meas[i], 0.0f };

        fft.perform (in.data(), measSpec.data(), false);
    }

    // d = IFFT( Y·conj(X) / (|X|² + ε·max|X|²) )。
    // 対数スイープのスペクトルは 1/f で落ちるため、無正則化の逆フィルタは
    // 高域で 1/|X|² の増幅になり、高調波成分が巨大な偽ピークとして現れる
    //（実測: 無正則化で THD が 700% 超）。ε は最大電力比で与え、
    // 励起が十分にある帯域だけを逆フィルタする
    double maxPower = 0.0;

    for (size_t i = 0; i < n; ++i)
        maxPower = std::max (maxPower, (double) refSpec[i].real() * refSpec[i].real()
                                            + (double) refSpec[i].imag() * refSpec[i].imag());

    const auto regularisation = 1.0e-3 * maxPower + 1.0e-12;

    std::vector<std::complex<float>> deconv ((size_t) n);

    for (size_t i = 0; i < n; ++i)
    {
        const auto x = std::complex<double> (refSpec[i]);
        const auto y = std::complex<double> (measSpec[i]);
        const auto power = x.real() * x.real() + x.imag() * x.imag();
        const auto d = y * std::conj (x) / (power + regularisation);
        deconv[i] = { (float) d.real(), (float) d.imag() };
    }

    std::vector<std::complex<float>> deconvTime ((size_t) n);
    fft.perform (deconv.data(), deconvTime.data(), true);

    // FFTのスケールは JUCE が正規化しないので、必要なのは比だけ（共通因子になる）ため省略
    auto sampleAt = [n, &deconvTime] (double lagSamples) -> double
    {
        auto index = (long long) std::llround (lagSamples);
        index %= (long long) n;

        if (index < 0)
            index += (long long) n;

        return std::abs (std::complex<double> (deconvTime[(size_t) index]));
    };

    // 線形応答のピーク位置を 0〜0.5 s から探す（I/O遅延 + インターフェース遅延）
    const auto searchEnd = std::min ((double) n, 0.5 * sampleRate);
    auto linearPeakLag = 0.0;
    auto linearPeakValue = 0.0;

    for (double lag = 0.0; lag < searchEnd; lag += 1.0)
    {
        const auto value = sampleAt (lag);

        if (value > linearPeakValue)
        {
            linearPeakValue = value;
            linearPeakLag = lag;
        }
    }

    if (linearPeakValue <= 0.0)
    {
        result.failureReason = juce::String (u8"スイープの応答が検出できませんでした");
        return result;
    }

    result.linearLagSamples = linearPeakLag;

    // ---- 各 IR を時間窓で切り出してスペクトルを求める（#114）----
    // 窓長は隣り合う高調波 IR の最小間隔（k=5 と 6 の間）の 90%。ピーク前に 10% を取る
    const auto logRatio = std::log (f2 / f1);
    const auto minGapSamples = sweepSeconds * std::log (6.0 / 5.0) / logRatio * sampleRate;
    const auto windowLength = (int) std::floor (0.9 * minGapSamples);

    if (windowLength < 64)
    {
        result.failureReason = juce::String (u8"スイープが短すぎて高調波を分離できません");
        return result;
    }

    const auto preSamples = std::max (4, windowLength / 10);
    int segmentOrder = 6;

    while ((1 << segmentOrder) < 2 * windowLength) // 2倍にゼロ詰めして周波数を細かく見る
        ++segmentOrder;

    const auto segmentSize = (size_t) 1 << segmentOrder;
    juce::dsp::FFT segmentFft (segmentOrder);

    // 窓: ピーク前は半 Hann で立ち上げ、末尾 25% を半 Hann で落とす
    std::vector<double> window ((size_t) windowLength, 1.0);
    const auto fallLength = std::max (4, windowLength / 4);

    for (int i = 0; i < windowLength; ++i)
    {
        if (i < preSamples)
            window[(size_t) i] = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi * (double) i / (double) preSamples));
        else if (i >= windowLength - fallLength)
            window[(size_t) i] = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi
                                                        * (double) (windowLength - 1 - i) / (double) fallLength));
    }

    // 正則化のゲイン g(ν) = |X|²/(|X|²+ε) を、セグメントの bin 幅で平均した |X|² から求める
    const auto segmentBins = segmentSize / 2 + 1;
    std::vector<double> regularisationGain (segmentBins, 1.0);

    {
        const auto ratio = (double) n / (double) segmentSize; // 大きい FFT の bin 数 / セグメント bin

        for (size_t j = 0; j < segmentBins; ++j)
        {
            const auto lo = (size_t) std::max (0.0, std::floor (((double) j - 0.5) * ratio));
            const auto hi = (size_t) std::min ((double) (n / 2), std::ceil (((double) j + 0.5) * ratio));
            double sum = 0.0;
            int count = 0;

            for (auto i = lo; i <= hi; ++i)
            {
                sum += std::norm (std::complex<double> (refSpec[i]));
                ++count;
            }

            const auto power = count > 0 ? sum / (double) count : 0.0;
            regularisationGain[j] = power / (power + regularisation);
        }
    }

    // k 次（1 = 線形）の IR を切り出した |H_k|² のスペクトル（正則化ゲイン補正済み）
    auto harmonicPowerSpectrum = [&] (int k)
    {
        const auto arrival = k == 1 ? 0.0 : LogSweepGenerator::harmonicArrivalSeconds (sweepSeconds, f1, f2, k);
        const auto peak = (long long) std::llround (linearPeakLag - arrival * sampleRate);
        std::vector<std::complex<float>> in (segmentSize), out (segmentSize);

        for (int i = 0; i < windowLength; ++i)
        {
            auto index = (peak - preSamples + i) % (long long) n;

            if (index < 0)
                index += (long long) n;

            in[(size_t) i] = { (float) (deconvTime[(size_t) index].real() * window[(size_t) i]), 0.0f };
        }

        segmentFft.perform (in.data(), out.data(), false);

        std::vector<double> power (segmentBins, 0.0);

        for (size_t j = 0; j < segmentBins; ++j)
        {
            const auto g = std::max (regularisationGain[j], 1.0e-3);
            power[j] = std::norm (std::complex<double> (out[j])) / (g * g);
        }

        return power;
    };

    std::array<std::vector<double>, 5> spectra; // [0] = 線形, [1..4] = 2〜5次

    for (int k = 1; k <= 5; ++k)
        spectra[(size_t) (k - 1)] = harmonicPowerSpectrum (k);

    const auto binHz = sampleRate / (double) segmentSize;

    // スイープ両端のフェード（LogSweepGenerator の既定 50 ms）区間では |X| が下がる。
    // 高調波はフェード前の基本波から生じるので、フェード区間の ν で Y_k/X を取ると過大になる。
    // フェードにかかる周波数は評価に使わない
    constexpr double sweepFadeSeconds = 0.05;
    const auto fadeRatio = std::exp (sweepFadeSeconds * logRatio / sweepSeconds);
    const auto lowerLimit = f1 * fadeRatio;
    const auto upperLimit = std::min (f2 / fadeRatio, 0.45 * sampleRate);

    // [lo, hi) Hz の bin 平均パワー（bin が無ければ中心に最も近い bin）
    auto meanPower = [&] (const std::vector<double>& power, double lo, double hi)
    {
        const auto first = (size_t) std::max (1.0, std::ceil (lo / binHz));
        const auto last = (size_t) std::min ((double) (segmentBins - 1), std::floor (hi / binHz));
        double sum = 0.0;
        int count = 0;

        for (auto j = first; j <= last && last >= first; ++j)
        {
            sum += power[j];
            ++count;
        }

        if (count == 0)
        {
            const auto nearest = (size_t) juce::jlimit (1.0, (double) (segmentBins - 1),
                                                        std::round (std::sqrt (lo * hi) / binHz));
            return power[nearest];
        }

        return sum / (double) count;
    };

    // 1/3 oct バンド（IEC 61260 の基数10）。基本波のバンドがフェード後の下限より上で、
    // 2次高調波のバンドがフェード前の上限（と 0.45·fs）に収まるところまで
    static constexpr double nominal[] = { 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0 };
    const auto halfBand = std::pow (10.0, 0.05);

    for (int band = -20; band <= 13; ++band)
    {
        const auto fc = 1000.0 * std::pow (10.0, (double) band / 10.0);

        if (fc / halfBand < lowerLimit || 2.0 * fc * halfBand > upperLimit)
            continue;

        const auto lo = fc / halfBand;
        const auto hi = fc * halfBand;
        const auto fundamentalPower = meanPower (spectra[0], lo, hi);

        if (! (fundamentalPower > 0.0))
            continue;

        ThdBand entry;
        entry.centerHz = fc;
        const auto decade = (int) std::floor ((double) band / 10.0);
        entry.nominalHz = nominal[band - decade * 10] * 1000.0 * std::pow (10.0, (double) decade);
        entry.fundamentalDb = 10.0 * std::log10 (fundamentalPower);

        double harmonicSum = 0.0;

        for (int k = 2; k <= 5; ++k)
        {
            if (k * hi > upperLimit)
                break; // 測れない高調波は 0 とみなす

            const auto ratio = meanPower (spectra[(size_t) (k - 1)], k * lo, k * hi) / fundamentalPower;
            harmonicSum += ratio;
            entry.harmonicPercent[(size_t) (k - 2)] = 100.0 * std::sqrt (ratio);
        }

        entry.thdPercent = 100.0 * std::sqrt (harmonicSum);
        result.bands.push_back (entry);
    }

    if (result.bands.empty())
    {
        result.failureReason = juce::String (u8"THD を求められる帯域がありません");
        return result;
    }

    // 代表値: 基本波が最大バンドから 40 dB 以内（応答の外れで S/N が悪いバンドを除く）の最大 THD
    double maxFundamentalDb = -1.0e300;

    for (const auto& band : result.bands)
        maxFundamentalDb = std::max (maxFundamentalDb, band.fundamentalDb);

    const ThdBand* worst = nullptr;

    for (const auto& band : result.bands)
        if (band.fundamentalDb >= maxFundamentalDb - 40.0 && (worst == nullptr || band.thdPercent > worst->thdPercent))
            worst = &band;

    result.thdPercent = worst->thdPercent;
    result.harmonicPercent = worst->harmonicPercent;
    result.worstFrequencyHz = worst->nominalHz;
    result.valid = true;
    return result;
}

} // namespace eeq::dsp
