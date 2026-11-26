#include "RtaSplAnalyzer.h"

#include <algorithm>

namespace eeq::dsp
{

namespace
{
constexpr int fftSize = 1 << RtaSplAnalyzer::fftOrder;
constexpr int hop = fftSize / 4; // 75%オーバーラップ: Fastの時定数より細かい更新間隔

double weightingFor (RtaSplAnalyzer::Weighting weighting, double freq)
{
    switch (weighting)
    {
        case RtaSplAnalyzer::Weighting::aWeighted: return RtaSplAnalyzer::aWeightingDb (freq);
        case RtaSplAnalyzer::Weighting::cWeighted: return RtaSplAnalyzer::cWeightingDb (freq);
        case RtaSplAnalyzer::Weighting::zWeighted: break;
    }

    return 0.0;
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

    const auto halfBandwidth = std::pow (2.0, 1.0 / (2.0 * bandsPerOctave));
    const auto fMin = 20.0;
    const auto fMax = std::min (20000.0, sampleRate * 0.5);

    for (auto f = fMin; f <= fMax * 1.01; f *= std::pow (2.0, 1.0 / (double) bandsPerOctave))
    {
        bandCenters.push_back (f);
        bandEnergy.push_back (0.0);
        bandPeak.push_back (0.0);
    }

    // 帯域境界は隣接する中心周波数の幾何平均（重なりなく敷き詰める）。
    // 両端だけは半オクターブ幅で外側に伸ばす
    const auto count = (int) bandCenters.size();

    for (int i = 0; i < count; ++i)
    {
        const auto loEdge = i == 0 ? bandCenters[0] / halfBandwidth
                                    : std::sqrt (bandCenters[(size_t) (i - 1)] * bandCenters[(size_t) i]);
        const auto hiEdge = i == count - 1 ? bandCenters[(size_t) i] * halfBandwidth
                                           : std::sqrt (bandCenters[(size_t) i] * bandCenters[(size_t) (i + 1)]);
        bandEdgesLo.push_back (loEdge);
        bandEdgesHi.push_back (hiEdge);
    }

    rtaOut.centerFreq = bandCenters;
    rtaOut.levelDb.assign (bandCenters.size(), -300.0f);
    rtaOut.peakHoldDb.assign (bandCenters.size(), -300.0f);
}

void RtaSplAnalyzer::setWeighting (Weighting newWeighting)
{
    const juce::ScopedLock sl (lock);
    weighting = newWeighting;
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
    binWeightDb.assign ((size_t) halfBins, 0.0);

    for (int k = 1; k < halfBins; ++k)
    {
        const auto re = frameBuffer[(size_t) k * 2];
        const auto im = frameBuffer[(size_t) k * 2 + 1];
        binPower[(size_t) k] = (double) re * re + (double) im * im;
        binWeightDb[(size_t) k] = weightingFor (weighting, (double) k * binWidth);
    }

    const auto frameDuration = hop / sampleRate;
    const auto rtaAlpha = 1.0 - std::exp (-frameDuration / std::max (0.05, rtaAveragingSeconds));
    // Fast / Slow は独立した時定数で常時それぞれ平滑する（表示切替時に
    // 収束待ちにならないようにするため、選択中の時定数だけを進める実装にはしない）
    const auto splAlphaFast = 1.0 - std::exp (-frameDuration / 0.125);
    const auto splAlphaSlow = 1.0 - std::exp (-frameDuration / 1.0);

    double frameEnergyWeighted = 0.0; // A/C/Z 重み付け済みの全帯域エネルギー

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

    // ---- SPL（現在の重み付けで全binを合算）----
    for (int k = 1; k < halfBins; ++k)
        frameEnergyWeighted += binPower[(size_t) k] * std::pow (10.0, binWeightDb[(size_t) k] / 10.0);

    fastEnergy += (frameEnergyWeighted - fastEnergy) * splAlphaFast;
    slowEnergy += (frameEnergyWeighted - slowEnergy) * splAlphaSlow;

    sessionEnergyTime += frameEnergyWeighted * frameDuration;
    sessionSeconds += frameDuration;

    slices.push_back ({ sessionSeconds, frameEnergyWeighted, frameDuration });

    while (! slices.empty() && sessionSeconds - slices.front().time > 900.0 + 1.0)
        slices.pop_front();

    hasSignal = true;

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

void RtaSplAnalyzer::copyRta (RtaFrame& out) const
{
    const juce::ScopedLock sl (lock);
    out = rtaOut;
}

void RtaSplAnalyzer::resetPeakHold()
{
    const juce::ScopedLock sl (lock);
    std::fill (bandPeak.begin(), bandPeak.end(), 0.0);
    rtaOut.peakHoldDb.assign (rtaOut.peakHoldDb.size(), -300.0f);
}

void RtaSplAnalyzer::copySpl (SplSnapshot& out) const
{
    const juce::ScopedLock sl (lock);

    out.sessionSeconds = sessionSeconds;
    out.hasSignal = hasSignal;
    out.currentDb = -300.0;
    out.leq1minDb = -300.0;
    out.leq15minDb = -300.0;
    out.leqSessionDb = -300.0;
    out.peakDb = -300.0;

    if (! hasSignal)
        return;

    const auto& energy = timeConstant == TimeConstant::fast ? fastEnergy : slowEnergy;
    // dBFS基準（フルスケール正弦波=0 dB）に正規化（RTAと同じ基準。仕様 3.4:
    // 校正しない状態のSPLは相対値で、校正オフセットを足して絶対値にする）
    out.currentDb = 10.0 * std::log10 (std::max (energy, 1.0e-30)) - sineRefPowerDb + calibrationOffsetDb;

    auto leqFor = [this] (double windowSeconds)
    {
        double sum = 0.0, duration = 0.0;

        for (auto it = slices.rbegin(); it != slices.rend(); ++it)
        {
            if (sessionSeconds - it->time > windowSeconds)
                break;

            sum += it->energy * it->duration;
            duration += it->duration;
        }

        return duration > 0.0 ? 10.0 * std::log10 (sum / duration) - sineRefPowerDb + calibrationOffsetDb : -300.0;
    };

    out.leq1minDb = leqFor (60.0);
    out.leq15minDb = leqFor (900.0);

    if (sessionEnergyTime > 0.0 && sessionSeconds > 0.0)
        out.leqSessionDb = 10.0 * std::log10 (sessionEnergyTime / sessionSeconds) - sineRefPowerDb
                         + calibrationOffsetDb;

    out.peakDb = 20.0 * std::log10 (std::max (peakSample, 1.0e-9)) + calibrationOffsetDb;
}

void RtaSplAnalyzer::resetSpl()
{
    const juce::ScopedLock sl (lock);

    fastEnergy = 0.0;
    slowEnergy = 0.0;
    sessionEnergyTime = 0.0;
    sessionSeconds = 0.0;
    peakSample = 0.0;
    hasSignal = false;
    slices.clear();
}

} // namespace eeq::dsp
