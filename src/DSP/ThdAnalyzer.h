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
struct ThdResult
{
    bool valid = false;
    juce::String failureReason;      // valid=false のときの理由（画面にそのまま出す）
    double thdPercent = 0.0;
    double linearLagSamples = 0.0;   // 線形応答のピーク位置（I/O遅延込み）
    std::array<double, 4> harmonicPercent {}; // 2次〜5次
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

    // 各高調波の窓RMS（±5 ms）
    auto windowRms = [&] (double lagSamples) -> double
    {
        const auto width = std::max (1.0, 0.005 * sampleRate);
        double sum = 0.0;
        int count = 0;

        for (double lag = lagSamples - width; lag <= lagSamples + width; lag += 1.0)
        {
            sum += sampleAt (lag) * sampleAt (lag);
            ++count;
        }

        return count > 0 ? std::sqrt (sum / (double) count) : 0.0;
    };

    const auto fundamental = windowRms (linearPeakLag);

    if (fundamental <= 0.0)
    {
        result.failureReason = juce::String (u8"スイープの応答が検出できませんでした");
        return result;
    }

    double harmonicPower = 0.0;

    for (int k = 2; k <= 5; ++k)
    {
        // k次高調波は線形応答より T·ln(k)/ln(f2/f1) だけ早い（負の時間側）位置に現れる
        const auto arrival = LogSweepGenerator::harmonicArrivalSeconds (sweepSeconds, f1, f2, k);
        const auto harmonicLag = linearPeakLag - arrival * sampleRate;
        const auto rms = windowRms (harmonicLag);

        harmonicPower += rms * rms;
        result.harmonicPercent[(size_t) (k - 2)] = 100.0 * rms / fundamental;
    }

    result.thdPercent = 100.0 * std::sqrt (harmonicPower) / fundamental;
    result.valid = true;
    return result;
}

} // namespace eeq::dsp
