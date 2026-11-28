#include "RtaSplAnalyzer.h"

#include <algorithm>
#include <complex>

namespace eeq::dsp
{

namespace
{
constexpr int fftSize = 1 << RtaSplAnalyzer::fftOrder;
constexpr int hop = fftSize / 4; // 75%オーバーラップ: Fastの時定数より細かい更新間隔

constexpr double sineMeanSquare = 0.5; // フルスケール正弦波の二乗平均（SPL の 0 dB 基準）
constexpr int splSliceSamples = hop;   // Leq スライスの長さ

// IEC 61672 の A/C 重み付けの極周波数 [Hz]（aWeightingDb / cWeightingDb と同じ値）
constexpr double poleF1 = 20.6;
constexpr double poleF2 = 107.7;
constexpr double poleF3 = 737.9;
constexpr double poleF4 = 12194.0;

double analogWeightingDb (RtaSplAnalyzer::Weighting weighting, double freq)
{
    switch (weighting)
    {
        case RtaSplAnalyzer::Weighting::aWeighted: return RtaSplAnalyzer::aWeightingDb (freq);
        case RtaSplAnalyzer::Weighting::cWeighted: return RtaSplAnalyzer::cWeightingDb (freq);
        case RtaSplAnalyzer::Weighting::zWeighted: break;
    }

    return 0.0;
}

// 1次の極 (s + ω) を双一次変換した z^-1 の多項式 (c0 + c1·z^-1)
std::array<double, 2> bilinearPole (double poleHz, double sampleRate)
{
    const auto k = 2.0 * sampleRate;
    const auto w = 2.0 * juce::MathConstants<double>::pi * poleHz;
    return { k + w, -(k - w) };
}

template <typename Section>
double sectionMagnitude (const Section& s, double freq, double sampleRate)
{
    const auto w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const auto z2 = z1 * z1;
    return std::abs ((s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2));
}
} // namespace

double RtaSplAnalyzer::aWeightingDb (double frequency)
{
    const auto f2 = frequency * frequency;
    const auto numerator = 12194.0 * 12194.0 * f2 * f2;
    const auto denominator = (f2 + 20.6 * 20.6)
                           * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
                           * (f2 + 12194.0 * 12194.0);

    if (denominator <= 0.0)
        return -300.0;

    // 1 kHz で 0 dB になるよう正規化（IEC 61672 の定義）
    const auto at1k = [ = ]
    {
        const auto k2 = 1000.0 * 1000.0;
        return 12194.0 * 12194.0 * k2 * k2
             / ((k2 + 20.6 * 20.6) * std::sqrt ((k2 + 107.7 * 107.7) * (k2 + 737.9 * 737.9))
                * (k2 + 12194.0 * 12194.0));
    }();

    return 20.0 * std::log10 (numerator / denominator / at1k);
}

double RtaSplAnalyzer::cWeightingDb (double frequency)
{
    const auto f2 = frequency * frequency;
    const auto numerator = 12194.0 * 12194.0 * f2;
    const auto denominator = (f2 + 20.6 * 20.6) * (f2 + 12194.0 * 12194.0);

    if (denominator <= 0.0)
        return -300.0;

    const auto at1k = [ = ]
    {
        const auto k2 = 1000.0 * 1000.0;
        return 12194.0 * 12194.0 * k2 / ((k2 + 20.6 * 20.6) * (k2 + 12194.0 * 12194.0));
    }();

    return 20.0 * std::log10 (numerator / denominator / at1k);
}

void RtaSplAnalyzer::WeightingFilter::design (Weighting type, double fs)
{
    numSections = 0;
    gain = 1.0;
    reset();

    if (type == Weighting::zWeighted || ! (fs > 0.0))
        return;

    // アナログ伝達関数（IEC 61672-1 附属書 E）
    //   A: s^4 / ((s+ω1)^2 (s+ω2)(s+ω3)(s+ω4)^2)
    //   C: s^2 / ((s+ω1)^2 (s+ω4)^2)
    // を双一次変換する。単純な双一次変換では余った極の数だけ z = −1 に零点が入り、
    // 48 kHz で 10 kHz −1.2 dB / 16 kHz −6.4 dB と高域が落ちる。そこで余りの零点を
    // z = −r に置き、r を定義式との最大誤差が最小になるよう設計時に数値探索する
    // （48 kHz で 20 Hz〜16 kHz の誤差 ±0.4 dB 以内）。
    const bool isA = type == Weighting::aWeighted;

    auto build = [&] (double r, WeightingFilter& out)
    {
        out.numSections = isA ? 3 : 2;
        out.gain = 1.0;
        out.reset();

        auto makeSection = [&] (double poleA, double poleB, bool dcZeros)
        {
            const auto pa = bilinearPole (poleA, fs);
            const auto pb = bilinearPole (poleB, fs);
            const auto a0 = pa[0] * pb[0];

            Biquad section;
            section.a1 = (pa[0] * pb[1] + pa[1] * pb[0]) / a0;
            section.a2 = (pa[1] * pb[1]) / a0;

            if (dcZeros) // (1 − z^-1)^2
            {
                section.b0 = 1.0;
                section.b1 = -2.0;
                section.b2 = 1.0;
            }
            else // (1 + r·z^-1)^2
            {
                section.b0 = 1.0;
                section.b1 = 2.0 * r;
                section.b2 = r * r;
            }

            return section;
        };

        if (isA)
        {
            out.sections[0] = makeSection (poleF1, poleF1, true);
            out.sections[1] = makeSection (poleF2, poleF3, true);
            out.sections[2] = makeSection (poleF4, poleF4, false);
        }
        else
        {
            out.sections[0] = makeSection (poleF1, poleF1, true);
            out.sections[1] = makeSection (poleF4, poleF4, false);
        }

        // 1 kHz で 0 dB に正規化
        double at1k = 1.0;

        for (int i = 0; i < out.numSections; ++i)
            at1k *= sectionMagnitude (out.sections[(size_t) i], 1000.0, fs);

        out.gain = at1k > 0.0 ? 1.0 / at1k : 1.0;
    };

    auto maxError = [&] (const WeightingFilter& candidate)
    {
        double worst = 0.0;
        const auto upper = std::min (20000.0, fs * 0.36);

        for (int n = -17; n <= 13; ++n)
        {
            const auto f = RtaSplAnalyzer::exactBandCenter (n, 10);

            if (f > upper)
                break;

            double mag = candidate.gain;

            for (int i = 0; i < candidate.numSections; ++i)
                mag *= sectionMagnitude (candidate.sections[(size_t) i], f, fs);

            worst = std::max (worst, std::abs (20.0 * std::log10 (std::max (mag, 1.0e-30))
                                               - analogWeightingDb (type, f)));
        }

        return worst;
    };

    double bestR = 1.0;
    double bestError = 1.0e30;
    WeightingFilter candidate;

    for (int i = 0; i <= 1000; ++i)
    {
        const auto r = (double) i / 1000.0;
        build (r, candidate);
        const auto error = maxError (candidate);

        if (error < bestError)
        {
            bestError = error;
            bestR = r;
        }
    }

    build (bestR, *this);
}

double RtaSplAnalyzer::weightingFilterResponseDb (Weighting weighting, double frequency, double fs)
{
    WeightingFilter filter;
    filter.design (weighting, fs);

    double mag = filter.gain;

    for (int i = 0; i < filter.numSections; ++i)
        mag *= sectionMagnitude (filter.sections[(size_t) i], frequency, fs);

    return 20.0 * std::log10 (std::max (mag, 1.0e-30));
}

double RtaSplAnalyzer::nominalBandFrequency (double exactCenter, int b)
{
    if (! (exactCenter > 0.0) || b <= 0)
        return exactCenter;

    const auto n = (int) std::lround ((double) b * std::log10 (exactCenter / 1000.0) / 0.3);

    // 1/3 oct の系列に乗る帯域は IEC 61260 の公称値（R10 系列）
    if ((n * 3) % b == 0)
    {
        static constexpr double mantissa[] = { 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0 };
        const auto m = n * 3 / b;
        const auto decade = (int) std::floor ((double) m / 10.0);
        const auto index = m - decade * 10;
        return mantissa[index] * 1000.0 * std::pow (10.0, (double) decade);
    }

    // それ以外は有効数字3桁に丸める
    const auto digits = (int) std::floor (std::log10 (exactCenter));
    const auto scale = std::pow (10.0, (double) (digits - 2));
    return std::round (exactCenter / scale) * scale;
}

void RtaSplAnalyzer::configure (double newSampleRate, int newBandsPerOctave, double newAveragingSeconds)
{
    const juce::ScopedLock sl (lock);

    const bool structural = ! configured || newSampleRate != sampleRate || newBandsPerOctave != bandsPerOctave;

    sampleRate = newSampleRate;
    bandsPerOctave = juce::jlimit (3, 24, newBandsPerOctave);
    rtaAveragingSeconds = juce::jlimit (0.1, 30.0, newAveragingSeconds);
    configured = true;

    if (structural)
    {
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        window.assign ((size_t) fftSize, 0.0f);

        double windowPower = 0.0;

        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = 0.5f
                               * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize));
            windowPower += (double) window[(size_t) i] * window[(size_t) i];
        }

        // SPL/RTA は正の周波数binのパワーを全帯域で合算するため、同じ規約で
        // フルスケール正弦波の基準を作る（非正規化DFT、DC/Nyquistを除く）。
        // Parseval より、正弦波の正周波数側の総和は N * sum(window^2) / 4。
        sineRefPowerDb = 10.0 * std::log10 (std::max (fftSize * windowPower * 0.25, 1.0e-30));

        // SPL の時間重み付け（IEC 61672: Fast 125 ms / Slow 1 s）と重み付けフィルタ
        fastAlpha = 1.0 - std::exp (-1.0 / (0.125 * sampleRate));
        slowAlpha = 1.0 - std::exp (-1.0 / (1.0 * sampleRate));
        mediumAlpha = 1.0 - std::exp (-1.0 / (0.5 * sampleRate));
        weightingFilter.design (weighting, sampleRate);

        assembly.assign ((size_t) fftSize, 0.0f);
        assemblyPos = 0;
        sinceFrame = 0;
        firstFrameDone = false;
        rebuildBands();
    }
}

void RtaSplAnalyzer::rebuildBands()
{
    bandCenters.clear();
    bandEdgesLo.clear();
    bandEdgesHi.clear();
    bandEnergy.clear();
    bandPeak.clear();
    rtaOut = {};

    // IEC 61260-1 の基数10: fc = 1000·G^(n/b)、G = 10^0.3。帯域端は fc·G^(±1/(2b))（#116）
    const auto b = (double) bandsPerOctave;
    const auto halfBandwidth = std::pow (10.0, 0.3 / (2.0 * b));

    // 下端は 20 Hz を含む帯域から、上端は 20 kHz を含む帯域まで。
    // ただし帯域の上端が fs/2 以上になる帯域は含めない
    const auto nMin = (int) std::ceil (b * std::log10 (20.0 / 1000.0) / 0.3 - 0.5);
    const auto nMax = (int) std::floor (b * std::log10 (20000.0 / 1000.0) / 0.3 + 0.5);

    std::vector<double> nominal;

    for (int n = nMin; n <= nMax; ++n)
    {
        const auto fc = exactBandCenter (n, bandsPerOctave);

        if (fc * halfBandwidth >= sampleRate * 0.5)
            break;

        bandCenters.push_back (fc);
        bandEdgesLo.push_back (fc / halfBandwidth);
        bandEdgesHi.push_back (fc * halfBandwidth);
        bandEnergy.push_back (0.0);
        bandPeak.push_back (0.0);
        nominal.push_back (nominalBandFrequency (fc, bandsPerOctave));
    }

    rtaOut.nominalFreq = nominal;
    rtaOut.centerFreq = bandCenters;
    rtaOut.levelDb.assign (bandCenters.size(), -300.0f);
    rtaOut.peakHoldDb.assign (bandCenters.size(), -300.0f);
}

void RtaSplAnalyzer::setWeighting (Weighting newWeighting)
{
    const juce::ScopedLock sl (lock);

    if (newWeighting == weighting)
        return;

    weighting = newWeighting;
    weightingFilter.design (weighting, sampleRate);

    // 重み付けを切り替えたら Leq とスライスをリセットする（A と C の混在を防ぐ: #119）。
    // ピークは Z 特性の生値なので保持する
    resetSplUnlocked (true);
}

void RtaSplAnalyzer::setTimeConstant (TimeConstant newTimeConstant)
{
    const juce::ScopedLock sl (lock);
    timeConstant = newTimeConstant;
}

void RtaSplAnalyzer::process (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    const juce::ScopedLock sl (lock);

    // fft は configure() がロック内で差し替えるため、判定もロック内で行う
    if (fft == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        // ピークは時間領域の生値（Z特性のピーク。校正オフセットは表示時に加える）
        peakSample = std::max (peakSample, (double) std::abs (samples[i]));
        processSplSample ((double) samples[i]);

        assembly[(size_t) assemblyPos] = samples[i];
        assemblyPos = (assemblyPos + 1) % fftSize;

        if (++sinceFrame >= hop && firstFrameDone)
        {
            sinceFrame = 0;
            processFrame();
        }
        else if (! firstFrameDone && assemblyPos == 0)
        {
            firstFrameDone = true; // 最初の1フレームがそろった
            sinceFrame = 0;
            processFrame();
        }
    }
}

void RtaSplAnalyzer::processFrame()
{
    frameBuffer.assign ((size_t) fftSize * 2, 0.0f);

    for (int i = 0; i < fftSize; ++i)
    {
        auto idx = assemblyPos - fftSize + i;

        while (idx < 0)
            idx += fftSize;

        frameBuffer[(size_t) i] = assembly[(size_t) idx] * window[(size_t) i];
    }

    fft->performRealOnlyForwardTransform (frameBuffer.data(), true);

    const auto halfBins = fftSize / 2 + 1;
    const auto binWidth = sampleRate / (double) fftSize;

    // bin パワー（窓は各bin共通なのでエネルギー比としては相対値で十分）
    binPower.assign ((size_t) halfBins, 0.0);

    for (int k = 1; k < halfBins; ++k)
    {
        const auto re = frameBuffer[(size_t) k * 2];
        const auto im = frameBuffer[(size_t) k * 2 + 1];
        binPower[(size_t) k] = (double) re * re + (double) im * im;
    }

    const auto frameDuration = hop / sampleRate;
    const auto rtaAlpha = 1.0 - std::exp (-frameDuration / std::max (0.05, rtaAveragingSeconds));

    // ---- RTAバンド ----
    for (size_t b = 0; b < bandCenters.size(); ++b)
    {
        double energy = 0.0;

        // 各binを中心 ± binWidth/2 の区間とみなし、帯域との重なりだけを按分する。
        // 隣接帯域は共有境界の片側ずつを受け取るため、境界binを二重計上しない。
        const auto firstBin = juce::jlimit (1, halfBins - 1,
                                            (int) std::floor (bandEdgesLo[b] / binWidth - 0.5));
        const auto lastBin = juce::jlimit (1, halfBins - 1,
                                           (int) std::ceil (bandEdgesHi[b] / binWidth + 0.5));

        for (int k = firstBin; k <= lastBin; ++k)
        {
            const auto binLo = ((double) k - 0.5) * binWidth;
            const auto binHi = ((double) k + 0.5) * binWidth;
            const auto overlap = std::max (0.0, std::min (bandEdgesHi[b], binHi)
                                                - std::max (bandEdgesLo[b], binLo)) / binWidth;
            energy += binPower[(size_t) k] * overlap;
        }

        bandEnergy[b] += (energy - bandEnergy[b]) * rtaAlpha;
        bandPeak[b] = std::max (bandPeak[b], energy);
    }

    // ---- 出力の組み立て ----
    rtaOut.sampleRate = sampleRate;

    for (size_t b = 0; b < bandCenters.size(); ++b)
    {
        // dBFS基準（フルスケール正弦波=0 dB）に正規化してから校正オフセットを足す。
        // 正規化しないと生のFFTパワー（数十dB）になり、表示レンジをオーバーして
        // バーが常に天井に張り付く
        rtaOut.levelDb[b] = (float) (10.0 * std::log10 (std::max (bandEnergy[b], 1.0e-30)) - sineRefPowerDb
                                       + calibrationOffsetDb);
        rtaOut.peakHoldDb[b] = (float) (10.0 * std::log10 (std::max (bandPeak[b], 1.0e-30)) - sineRefPowerDb
                                          + calibrationOffsetDb);
    }
}

void RtaSplAnalyzer::processSplSample (double sample)
{
    // 時間領域の SPL（IEC 61672）: 重み付け → 二乗 → 指数平滑（#111）
    auto y = weightingFilter.process (std::isfinite (sample) ? sample : 0.0);

    if (! std::isfinite (y)) // フィルタ状態が壊れたら作り直す
    {
        weightingFilter.reset();
        y = 0.0;
    }

    const auto energy = y * y;
    fastEnergy += (energy - fastEnergy) * fastAlpha;
    slowEnergy += (energy - slowEnergy) * slowAlpha;
    mediumEnergy += (energy - mediumEnergy) * mediumAlpha;

    pendingEnergy += energy;
    hasSignal = true;

    if (++pendingSamples >= splSliceSamples)
    {
        const auto duration = (double) pendingSamples / sampleRate;
        sessionEnergyTime += pendingEnergy / sampleRate;
        sessionSeconds += duration;
        slices.push_back ({ sessionSeconds, pendingEnergy / (double) pendingSamples, duration });

        while (! slices.empty() && sessionSeconds - slices.front().time > 900.0 + 1.0)
            slices.pop_front();

        pendingEnergy = 0.0;
        pendingSamples = 0;
    }
}

void RtaSplAnalyzer::copyRta (RtaFrame& out) const
{
    const juce::ScopedLock sl (lock);
    out = rtaOut;
}

bool RtaSplAnalyzer::tryCopyRta (RtaFrame& out) const
{
    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked())
        return false;

    out = rtaOut;
    return true;
}

void RtaSplAnalyzer::resetPeakHold()
{
    const juce::ScopedLock sl (lock);
    std::fill (bandPeak.begin(), bandPeak.end(), 0.0);
    rtaOut.peakHoldDb.assign (rtaOut.peakHoldDb.size(), -300.0f);
}

void RtaSplAnalyzer::copySplUnlocked (SplSnapshot& out) const
{
    out.sessionSeconds = sessionSeconds;
    out.hasSignal = hasSignal;
    out.currentDb = -300.0;
    out.leq1minDb = -300.0;
    out.leq15minDb = -300.0;
    out.leqSessionDb = -300.0;
    out.peakDb = -300.0;

    if (! hasSignal)
        return;

    const auto& energy = timeConstant == TimeConstant::fast ? fastEnergy
                       : timeConstant == TimeConstant::slow ? slowEnergy : mediumEnergy;
    // dBFS基準（フルスケール正弦波=0 dB）に正規化（RTAと同じ基準。仕様 3.4:
    // 校正しない状態のSPLは相対値で、校正オフセットを足して絶対値にする）
    auto toDb = [this] (double meanSquare)
    { return 10.0 * std::log10 (std::max (meanSquare, 1.0e-30) / sineMeanSquare) + calibrationOffsetDb; };

    out.currentDb = toDb (energy);

    const auto pendingSeconds = (double) pendingSamples / sampleRate;

    auto leqFor = [&] (double windowSeconds)
    {
        // スライスにまだまとめていない直近のサンプルも含める
        double sum = pendingEnergy / sampleRate, duration = pendingSeconds;

        for (auto it = slices.rbegin(); it != slices.rend(); ++it)
        {
            if (sessionSeconds - it->time > windowSeconds)
                break;

            sum += it->energy * it->duration;
            duration += it->duration;
        }

        return duration > 0.0 ? toDb (sum / duration) : -300.0;
    };

    out.leq1minDb = leqFor (60.0);
    out.leq15minDb = leqFor (900.0);

    const auto totalSeconds = sessionSeconds + pendingSeconds;
    const auto totalEnergyTime = sessionEnergyTime + pendingEnergy / sampleRate;

    if (totalEnergyTime > 0.0 && totalSeconds > 0.0)
        out.leqSessionDb = toDb (totalEnergyTime / totalSeconds);

    out.sessionSeconds = totalSeconds;

    // Lpeak: 0 dB は正弦波の RMS 基準なので、ピーク振幅 |x| は √2 倍（+3.01 dB）して換算する（#112）。
    // （フルスケール正弦波は Leq 0 dB、Lpeak +3.01 dB）
    if (peakSample > 0.0)
        out.peakDb = 20.0 * std::log10 (peakSample * std::sqrt (2.0)) + calibrationOffsetDb;
}

void RtaSplAnalyzer::copySpl (SplSnapshot& out) const
{
    const juce::ScopedLock sl (lock);
    copySplUnlocked (out);
}

bool RtaSplAnalyzer::tryCopySpl (SplSnapshot& out) const
{
    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked())
        return false;

    copySplUnlocked (out);
    return true;
}

void RtaSplAnalyzer::copyAll (RtaFrame& rtaFrameOut, SplSnapshot& splOut) const
{
    // RTAとSPLを1回のロックで取る（issue #37）。同一時点の値が取れるという副次効果もある
    const juce::ScopedLock sl (lock);
    rtaFrameOut = rtaOut;
    copySplUnlocked (splOut);
}

bool RtaSplAnalyzer::tryCopyAll (RtaFrame& rtaFrameOut, SplSnapshot& splOut) const
{
    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked())
        return false;

    rtaFrameOut = rtaOut;
    copySplUnlocked (splOut);
    return true;
}

void RtaSplAnalyzer::resetSpl()
{
    const juce::ScopedLock sl (lock);
    resetSplUnlocked (false);
}

void RtaSplAnalyzer::resetSplUnlocked (bool keepPeak)
{
    weightingFilter.reset();
    fastEnergy = 0.0;
    slowEnergy = 0.0;
    mediumEnergy = 0.0;
    pendingEnergy = 0.0;
    pendingSamples = 0;
    sessionEnergyTime = 0.0;
    sessionSeconds = 0.0;

    if (! keepPeak)
        peakSample = 0.0;

    hasSignal = false;
    slices.clear();
}

} // namespace eeq::dsp
