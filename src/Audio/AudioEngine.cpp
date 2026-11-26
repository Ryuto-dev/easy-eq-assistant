#include "AudioEngine.h"

#include "../Core/Log.h"

#include <cmath>

namespace eeq::audio
{
namespace
{
void measureChannel (const float* const* data, int numChannels, int numSamples, int index, MeterState& meter,
                     float clipThreshold)
{
    if (index < 0 || index >= numChannels || data[index] == nullptr)
    {
        meter.peakLinear.store (0.0f, std::memory_order_relaxed);
        meter.rmsLinear.store (0.0f, std::memory_order_relaxed);
        return;
    }

    float peak = 0.0f;
    double sumSquares = 0.0;
    const auto* channel = data[index];

    for (int i = 0; i < numSamples; ++i)
    {
        const auto magnitude = std::abs (channel[i]);
        peak = juce::jmax (peak, magnitude);
        sumSquares += (double) magnitude * (double) magnitude;
    }

    meter.peakLinear.store (peak, std::memory_order_relaxed);
    meter.rmsLinear.store ((float) std::sqrt (sumSquares / (double) numSamples), std::memory_order_relaxed);

    if (peak >= clipThreshold)
        meter.clip.store (true, std::memory_order_relaxed);
}

juce::String channelDisplayName (int index, const juce::String& name, bool input)
{
    auto label = name;

    if (label.isEmpty())
        label = input ? juce::String (u8"入力") : juce::String (u8"出力");

    return juce::String (index + 1) + ": " + label;
}
} // namespace

AudioEngine::AudioEngine (juce::PropertiesFile* settingsToUse)
    : settings (settingsToUse),
      measurementRing (1 << 18),
      measurement (measurementRing)
{
    formatManager.registerBasicFormats();
    measurement.setSplAnalyzer (&splAnalyzer);
    deviceManager.addAudioCallback (this);
    // デバイスは開かない。マイク許可の確定後に MainComponent が openInitialDevice() を呼ぶ
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

//==============================================================================
// デバイス（入力と出力は別々に選べる）

juce::StringArray AudioEngine::getAvailableDeviceNames (bool wantInputNames) const
{
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        return type->getDeviceNames (wantInputNames);

    return {};
}

juce::String AudioEngine::getSelectedInputDeviceName() const
{
    return deviceManager.getAudioDeviceSetup().inputDeviceName;
}

juce::String AudioEngine::getSelectedOutputDeviceName() const
{
    return deviceManager.getAudioDeviceSetup().outputDeviceName;
}

juce::String AudioEngine::selectInputDevice (const juce::String& deviceName)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.inputDeviceName = deviceName;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 64, true);

    auto error = deviceManager.setAudioDeviceSetup (setup, true);
    eeq::log::write (error.isEmpty() ? "input device changed to \"" + deviceName + "\""
                                     : "input device change to \"" + deviceName + "\" FAILED: " + error);

    if (error.isEmpty())
        saveSettings();

    return error;
}

juce::String AudioEngine::selectOutputDevice (const juce::String& deviceName)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.outputDeviceName = deviceName;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 64, true);

    auto error = deviceManager.setAudioDeviceSetup (setup, true);
    eeq::log::write (error.isEmpty() ? "output device changed to \"" + deviceName + "\""
                                     : "output device change to \"" + deviceName + "\" FAILED: " + error);

    if (error.isEmpty())
        saveSettings();

    return error;
}

void AudioEngine::reopenDevices()
{
    auto setup = deviceManager.getAudioDeviceSetup();
    deviceManager.closeAudioDevice();

    // 閉じた直後は currentAudioDevice が無いので、同一セットアップでも再オープンされる
    const auto error = deviceManager.setAudioDeviceSetup (setup, false);

    eeq::log::write (error.isEmpty() ? "devices reopened" : "device reopen FAILED: " + error);
}

juce::String AudioEngine::applyRateAndBuffer (double sampleRate, int bufferSize)
{
    auto setup = deviceManager.getAudioDeviceSetup();

    if (sampleRate > 0.0)
        setup.sampleRate = sampleRate;
    if (bufferSize > 0)
        setup.bufferSize = bufferSize;

    return deviceManager.setAudioDeviceSetup (setup, true);
}

juce::Array<double> AudioEngine::getAvailableSampleRates() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getAvailableSampleRates();

    return {};
}

juce::Array<int> AudioEngine::getAvailableBufferSizes() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getAvailableBufferSizes();

    return {};
}

void AudioEngine::getOpenedInputChannels (juce::Array<int>& indicesOut, juce::StringArray& namesOut) const
{
    indicesOut.clearQuick();
    namesOut.clearQuick();

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto active = device->getActiveInputChannels();
        const auto names = device->getInputChannelNames();

        for (int i = 0; i < names.size(); ++i)
        {
            if (! active[i])
                continue;
            indicesOut.add (i);
            namesOut.add (channelDisplayName (i, names[i], true));
        }
    }
}

void AudioEngine::getOpenedOutputChannels (juce::Array<int>& indicesOut, juce::StringArray& namesOut) const
{
    indicesOut.clearQuick();
    namesOut.clearQuick();

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto active = device->getActiveOutputChannels();
        const auto names = device->getOutputChannelNames();

        for (int i = 0; i < names.size(); ++i)
        {
            if (! active[i])
                continue;
            indicesOut.add (i);
            namesOut.add (channelDisplayName (i, names[i], false));
        }
    }
}

juce::String AudioEngine::getStatusText() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        const auto inputName = setup.inputDeviceName.isNotEmpty()
                                 ? setup.inputDeviceName
                                 : juce::String (u8"(既定)");
        const auto outputName = setup.outputDeviceName.isNotEmpty()
                                  ? setup.outputDeviceName
                                  : juce::String (u8"(既定)");

        return juce::String (u8"入力: ") + inputName + "   /   " + juce::String (u8"出力: ") + outputName
             + "   /   " + juce::String (device->getCurrentSampleRate(), 0) + " Hz   /   "
             + juce::String (device->getCurrentBufferSizeSamples()) + " samples";
    }

    return {};
}

//==============================================================================
// メーター・クリップ

void AudioEngine::clearClipLatches() noexcept
{
    refMeter.clip.store (false, std::memory_order_relaxed);
    measMeter.clip.store (false, std::memory_order_relaxed);
    outMeter.clip.store (false, std::memory_order_relaxed);
}

//==============================================================================
// 励起信号（ピンクノイズ / スイープ / WAV / 外部入力）

void AudioEngine::startExcitation() noexcept
{
    eeq::log::write ("excitation: start requested");
    noiseCommand.store (NoiseCommand::start, std::memory_order_relaxed);
}

void AudioEngine::stopExcitation() noexcept
{
    eeq::log::write ("excitation: stop requested");
    noiseCommand.store (NoiseCommand::stop, std::memory_order_relaxed);
}

void AudioEngine::emergencyStop() noexcept
{
    eeq::log::write ("EMERGENCY STOP (Esc): stopping output and measurement");
    noiseCommand.store (NoiseCommand::stop, std::memory_order_relaxed);
    measurement.stopMeasurement();
}

bool AudioEngine::isExcitationAudible() const noexcept
{
    const auto state = noiseState.load (std::memory_order_relaxed);
    return state == NoiseState::fadeIn || state == NoiseState::running;
}

void AudioEngine::publishExcitationBuffer (std::vector<float> samples)
{
    // 旧バッファを ownedBuffers[1] へ退避してから差し替える（issue #33）。
    // オーディオスレッドが activeBufferPtr を読んでいる最中に旧バッファが解放される
    // ことがないよう、直前の世代を次の差し替えまで保持する
    ownedBuffers[1] = std::move (ownedBuffers[0]);
    ownedBuffers[0] = std::make_shared<const std::vector<float>> (std::move (samples));
    activeBufferPtr.store (ownedBuffers[0].get(), std::memory_order_release);
}

void AudioEngine::setSweep (std::vector<float> sweepSamples)
{
    stopExcitation();

    const auto logLength = (double) sweepSamples.size() / std::max (1.0, currentSampleRate.load());
    eeq::log::write ("excitation: sweep set (" + juce::String (sweepSamples.size()) + " samples, "
                     + juce::String (logLength, 2) + " s)");

    publishExcitationBuffer (std::move (sweepSamples));
    excitationSource.store (ExcitationSource::sweep, std::memory_order_relaxed);
}

bool AudioEngine::loadWavExcitation (const juce::File& file, juce::String& errorOut)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        errorOut = juce::String (u8"読み込める音源ファイルがありません: ") + file.getFullPathName();
        eeq::log::write ("excitation: WAV load FAILED: " + errorOut);
        return false;
    }

    // 20分を超えるファイルは読み込まない（メモリ保護）
    const auto length = (juce::int64) juce::jmin (reader->lengthInSamples,
                                                  (juce::int64) 20 * 60 * (juce::int64) reader->sampleRate);

    if (length <= 0)
    {
        errorOut = juce::String (u8"空のファイルです: ") + file.getFullPathName();
        return false;
    }

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) length);
    reader->read (&buffer, 0, (int) length, 0, true, true);

    // 励起信号はモノラルで十分（系統の1chに出す）。ステレオは平均する
    std::vector<float> mono ((size_t) length, 0.0f);
    const auto channels = buffer.getNumChannels();

    float peak = 0.0f;

    for (int i = 0; i < (int) length; ++i)
    {
        float sum = 0.0f;

        for (int ch = 0; ch < channels; ++ch)
            sum += buffer.getSample (ch, i);

        const auto value = sum / (float) channels;
        peak = juce::jmax (peak, std::abs (value));
        mono[(size_t) i] = value;
    }

    // WAVはピーク1.0へ正規化して保持する（issue #29）。レベル設定（dBFS）は
    // オーディオスレッドでピーク基準として掛かるので、どのファイルでも
    // 「設定した dBFS ＝ ピーク」が一致する
    if (peak > 1.0e-9f)
        for (auto& value : mono)
            value /= peak;

    stopExcitation();
    eeq::log::write ("excitation: WAV loaded \"" + file.getFullPathName() + "\" ("
                     + juce::String ((double) length / std::max (1.0, (double) reader->sampleRate), 2) + " s)");

    publishExcitationBuffer (std::move (mono));
    excitationSource.store (ExcitationSource::wavFile, std::memory_order_relaxed);

    return true;
}

double AudioEngine::getExcitationProgress() const noexcept
{
    if (const auto source = excitationSource.load (std::memory_order_relaxed);
        source == ExcitationSource::sweep || source == ExcitationSource::wavFile)
    {
        // ロックフリー読み（issue #33）。ポインタは publishExcitationBuffer が公開する
        if (const auto* buffer = activeBufferPtr.load (std::memory_order_acquire);
            buffer != nullptr && ! buffer->empty())
            return juce::jlimit (0.0, 1.0, (double) playbackPosition.load (std::memory_order_relaxed)
                                            / (double) buffer->size());
    }

    return 0.0;
}

void AudioEngine::startCalibrationSignal (CalibrationSignal signal)
{
    switch (signal)
    {
        case CalibrationSignal::tone1k:
            excitationSource.store (ExcitationSource::tone1k, std::memory_order_relaxed);
            break;

        case CalibrationSignal::bandNoise1k:
            excitationSource.store (ExcitationSource::bandNoise1k, std::memory_order_relaxed);
            break;

        case CalibrationSignal::pinkBroadband:
        default:
            excitationSource.store (ExcitationSource::pinkNoise, std::memory_order_relaxed);
            break;
    }

    // 校正信号は現在の出力レベル設定（既定 −20 dBFS）で出す
    noiseCommand.store (NoiseCommand::start, std::memory_order_relaxed);
    eeq::log::write ("calibration signal: started");
}

//==============================================================================
// デバイスエラー

bool AudioEngine::consumeDeviceError (juce::String& messageOut)
{
    if (! hasPendingError.exchange (false, std::memory_order_relaxed))
        return false;

    const juce::ScopedLock lock (errorMutex);
    messageOut = pendingErrorMessage;
    return true;
}

//==============================================================================
// 設定

void AudioEngine::saveSettings()
{
    if (settings == nullptr)
        return;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto setup = deviceManager.getAudioDeviceSetup();
        settings->setValue ("inputDeviceName", setup.inputDeviceName);
        settings->setValue ("outputDeviceName", setup.outputDeviceName);
        settings->setValue ("sampleRate", device->getCurrentSampleRate());
        settings->setValue ("bufferSize", device->getCurrentBufferSizeSamples());
    }

    settings->setValue ("refChannel", refChannel.load (std::memory_order_relaxed));
    settings->setValue ("measChannel", measChannel.load (std::memory_order_relaxed));
    settings->setValue ("outputChannel", outputChannel.load (std::memory_order_relaxed));
    settings->setValue ("outputMask", juce::String ((juce::int64) outputMask.load (std::memory_order_relaxed)));
    settings->setValue ("noiseLevelDb", (double) noiseLevelDb.load (std::memory_order_relaxed));
    settings->setValue ("outputCeilingDb", (double) outputCeilingDb.load (std::memory_order_relaxed));
    settings->saveIfNeeded();
}

//==============================================================================
// AudioIODeviceCallback

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const auto refIndex = refChannel.load (std::memory_order_relaxed);
    const auto measIndex = measChannel.load (std::memory_order_relaxed);
    const auto refIsInternal = refIndex == internalRefChannel;

    measureChannel (inputChannelData, numInputChannels, numSamples, refIndex, refMeter, clipThresholdLinear);
    measureChannel (inputChannelData, numInputChannels, numSamples, measIndex, measMeter, clipThresholdLinear);

    const auto level = juce::Decibels::decibelsToGain (noiseLevelDb.load (std::memory_order_relaxed));
    const auto source = excitationSource.load (std::memory_order_relaxed);
    const auto routingMask = outputMask.load (std::memory_order_relaxed);
    auto state = noiseState.load (std::memory_order_relaxed);
    auto position = fadePosition.load (std::memory_order_relaxed);
    auto playPos = playbackPosition.load (std::memory_order_relaxed);

    // 校正信号（1kHz帯域制限ノイズ）のフィルタ係数。サンプルレートが変わったら作り直す
    //（オーディオスレッドだけが触る状態なのでロックは不要）
    if (source == ExcitationSource::bandNoise1k && bandNoiseCoeffsRate != currentSampleRate.load())
    {
        const auto sr = currentSampleRate.load (std::memory_order_relaxed);
        const auto w0 = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
        const auto alpha = std::sin (w0) / (2.0 * 1.414); // Q = 1.414（2次バンドパス）
        const auto b0 = alpha;
        const auto b2 = -alpha;
        const auto a0 = 1.0 + alpha;
        const auto a1 = -2.0 * std::cos (w0);
        const auto a2 = 1.0 - alpha;

        bandNoiseCoeffs[0] = b0 / a0;
        bandNoiseCoeffs[1] = 0.0;
        bandNoiseCoeffs[2] = b2 / a0;
        bandNoiseCoeffs[3] = a1 / a0;
        bandNoiseCoeffs[4] = a2 / a0;
        bandNoiseCoeffsRate = sr;
    }

    // UIからの開始/停止コマンドをブロックの先頭で反映する（状態はオーディオスレッドが所有する）
    switch (noiseCommand.exchange (NoiseCommand::none, std::memory_order_relaxed))
    {
        case NoiseCommand::start:
            if (state == NoiseState::idle || state == NoiseState::fadeOut)
            {
                state = NoiseState::fadeIn;
                position = 0;
                playPos = 0;
                playbackPosition.store (0, std::memory_order_relaxed);
            }
            break;

        case NoiseCommand::stop:
            if (state == NoiseState::running || state == NoiseState::fadeIn)
            {
                state = NoiseState::fadeOut;
                position = 0;
            }
            break;

        case NoiseCommand::none:
            break;
    }

    // オーディオスレッドで使う値はブロックの先頭で一度だけ読む（リアルタイム性）。
    // バッファは atomic な生ポインタを acquire 読みするだけで、ロックは行わない（issue #33）
    const auto* activeSweep = (source == ExcitationSource::sweep || source == ExcitationSource::wavFile)
                                ? activeBufferPtr.load (std::memory_order_acquire)
                                : nullptr;
    const auto ceilingGain = juce::Decibels::decibelsToGain (outputCeilingDb.load (std::memory_order_relaxed));

    const float* refIn = (refIndex >= 0 && refIndex < numInputChannels) ? inputChannelData[refIndex] : nullptr;
    const float* measIn = (measIndex >= 0 && measIndex < numInputChannels) ? inputChannelData[measIndex] : nullptr;
    const auto streamCount = juce::jmin (numSamples, (int) refStream.size());

    float outPeak = 0.0f;
    double outSumSquares = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // フェード包絡（0..1）。レベル設定はブロック先頭の level、ソースごとの正規化は
        // 下の分岐で行い、最後に一括して掛ける（issue #29: レベル定義の統一）
        float envelope = 0.0f;

        if (state == NoiseState::fadeIn)
        {
            const auto length = juce::jmax (1, fadeInLength.load (std::memory_order_relaxed));
            envelope = (float) position / (float) length;

            if (++position >= length)
            {
                position = 0;
                state = NoiseState::running;
            }
        }
        else if (state == NoiseState::running)
        {
            envelope = 1.0f;
        }
        else if (state == NoiseState::fadeOut)
        {
            const auto length = juce::jmax (1, fadeOutLength.load (std::memory_order_relaxed));
            envelope = 1.0f - (float) position / (float) length;

            if (++position >= length)
            {
                position = 0;
                state = NoiseState::idle;
                noiseReachedIdle.store (true, std::memory_order_relaxed);
            }
        }

        // 励起信号ソースを選ぶ（仕様 3.2）
        float sourceSample = 0.0f;

        switch (source)
        {
            case ExcitationSource::pinkNoise:
                // 生成器が実効RMS≈1.0に正規化済み → 設定値=「dBFS RMS」
                sourceSample = noise.nextSample();
                break;

            case ExcitationSource::sweep:
            case ExcitationSource::wavFile:
            {
                // スイープ: ピーク1.0で生成されているので 1/√2 を掛けて実効RMS基準に揃える
                // WAV: 読み込み時にピーク1.0へ正規化済み → 設定値=「dBFS ピーク」
                const auto normalise = source == ExcitationSource::sweep ? 0.70710678f : 1.0f;

                if (activeSweep != nullptr && playPos < (int) activeSweep->size())
                {
                    sourceSample = (*activeSweep)[(size_t) playPos] * normalise;
                    ++playPos;
                }
                else if (state == NoiseState::running)
                {
                    // 最後まで再生したらフェードアウトしてUIへ通知する
                    state = NoiseState::fadeOut;
                    position = 0;
                    excitationFinished.store (true, std::memory_order_relaxed);
                }
                break;
            }

            case ExcitationSource::external:
                sourceSample = 0.0f; // アプリは出力せず取り込みだけ（仕様 3.2）
                break;

            case ExcitationSource::tone1k:
            {
                // SPL校正用の1kHzトーン（安定した読み取り用。仕様 3.4）
                // 1/√2 を掛けて正弦波の実効RMSを設定値に揃える
                tonePhase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / std::max (8000.0, currentSampleRate.load());

                if (tonePhase > 2.0 * juce::MathConstants<double>::pi)
                    tonePhase -= 2.0 * juce::MathConstants<double>::pi;

                sourceSample = (float) std::sin (tonePhase) * 0.70710678f;
                break;
            }

            case ExcitationSource::bandNoise1k:
            {
                // 比較校正の既定信号: 1kHz中心の帯域制限ピンクノイズ（仕様 3.4）
                const auto pink = noise.nextSample();
                const auto b0 = bandNoiseCoeffs[0], b2 = bandNoiseCoeffs[2];
                const auto a1 = bandNoiseCoeffs[3], a2 = bandNoiseCoeffs[4];

                const auto out = b0 * pink + b2 * bandNoiseX2 - a1 * bandNoiseY1 - a2 * bandNoiseY2;

                bandNoiseX2 = bandNoiseX1;
                bandNoiseX1 = pink;
                bandNoiseY2 = bandNoiseY1;
                bandNoiseY1 = out;

                // 校正オフセットは表示側で決めるため絶対値は問題にならない。
                // 持ち上げ過ぎると ±1.0 のクランプで校正信号自身が歪むため、
                // 0.6 倍に留める（出力 RMS ≈ 0.19、ピーク ≈ 0.86 でクリップなし。
                // 入力は RMS≈1.0 に正規化されたピンクノイズ。バンドパス出力の
                // 実測: RMS 0.314 / ピーク 1.44）
                sourceSample = (float) juce::jlimit (-1.0, 1.0, out * 0.6);
                break;
            }
        }

        // 出力のハード上限（仕様 3.7 の安全設計）。包絡とレベルを掛けた最終値に適用する
        const auto sample = juce::jlimit (-ceilingGain, ceilingGain, sourceSample * envelope * level);

        outPeak = juce::jmax (outPeak, std::abs (sample));
        outSumSquares += (double) sample * (double) sample;

        // 系統ルーティング（仕様 3.3.4）: マスクが立っている出力chへ同じ励起信号を出す
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (auto* out = outputChannelData[ch])
                out[i] = ((routingMask & ((uint64_t) 1 << ch)) != 0) ? sample : 0.0f;

        // 測定ストリーム（仕様 2. の (1)）: Ref/Meas のペアをリングバッファへ書く
        if (i < streamCount)
        {
            refStream[(size_t) i] = refIsInternal ? sample : (refIn != nullptr ? refIn[i] : 0.0f);
            measStream[(size_t) i] = (measIn != nullptr ? measIn[i] : 0.0f);
        }
    }

    playbackPosition.store (playPos, std::memory_order_relaxed);

    if (streamCount > 0)
        measurementRing.write (refStream.data(), measStream.data(), streamCount);

    fadePosition.store (position, std::memory_order_relaxed);
    noiseState.store (state, std::memory_order_relaxed);

    outMeter.peakLinear.store (outPeak, std::memory_order_relaxed);
    outMeter.rmsLinear.store ((float) std::sqrt (outSumSquares / (double) juce::jmax (1, numSamples)),
                              std::memory_order_relaxed);

    // 内部参照（仕様 3.1 (b)）: アプリが出力した信号そのものを基準信号として計上する
    if (refIsInternal)
    {
        refMeter.peakLinear.store (outPeak, std::memory_order_relaxed);
        refMeter.rmsLinear.store ((float) std::sqrt (outSumSquares / (double) juce::jmax (1, numSamples)),
                                  std::memory_order_relaxed);

        if (outPeak >= clipThresholdLinear)
            refMeter.clip.store (true, std::memory_order_relaxed);
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    auto sampleRate = device->getCurrentSampleRate();

    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    // レート/バッファが変わったときだけログに残す
    if (std::abs (currentSampleRate.load() - sampleRate) > 0.5
        || currentBufferSize.load() != device->getCurrentBufferSizeSamples())
    {
        eeq::log::write ("device restarted: rate=" + juce::String (sampleRate, 0)
                         + " buffer=" + juce::String (device->getCurrentBufferSizeSamples()));
    }

    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    currentBufferSize.store (device->getCurrentBufferSizeSamples(), std::memory_order_relaxed);

    // 測定ストリーム用のスクラッチ（ここはメッセージスレッドなので確保してよい）
    const auto needed = juce::jmax (8192, device->getCurrentBufferSizeSamples());

    if ((int) refStream.size() < needed)
    {
        refStream.resize ((size_t) needed);
        measStream.resize ((size_t) needed);
    }

    fadeInLength.store ((int) (fadeInSeconds * sampleRate), std::memory_order_relaxed);
    fadeOutLength.store ((int) (fadeOutSeconds * sampleRate), std::memory_order_relaxed);
    deviceGeneration.fetch_add (1, std::memory_order_relaxed);

    // デバイスが変わると前のデバイスのチャンネル番号が範囲外になることがある。
    // 範囲外のままでは出力がどこにも届かず、メーターだけが動くので、ここで直す
    const auto numIn = device->getActiveInputChannels().countNumberOfSetBits();
    const auto numOut = device->getActiveOutputChannels().countNumberOfSetBits();

    auto fixChannel = [] (int channel, int availableChannels)
    {
        if (availableChannels <= 0)
            return -1;
        if (channel < 0)
            return -1;
        return juce::jlimit (0, availableChannels - 1, channel);
    };

    const auto currentRef = refChannel.load (std::memory_order_relaxed);

    if (currentRef != internalRefChannel)
    {
        // 入力が範囲外になったら、2ch以上なら末尾ch、1chなら内部参照へ
        const auto fixed = numIn == 0 ? internalRefChannel
                         : currentRef >= numIn ? (numIn > 1 ? numIn - 1 : internalRefChannel)
                         : currentRef;
        refChannel.store (fixed, std::memory_order_relaxed);
    }

    measChannel.store (fixChannel (measChannel.load (std::memory_order_relaxed), numIn), std::memory_order_relaxed);
    outputChannel.store (fixChannel (outputChannel.load (std::memory_order_relaxed), numOut), std::memory_order_relaxed);

    // 系統ルーティングマスクが新しいデバイスの出力chに1つも乗っていないときは、
    // 出力がどこにも届かなくなるので固定したchへ張り直す
    {
        const auto mask = outputMask.load (std::memory_order_relaxed);

        if (mask == 0 || numOut <= 0 || (mask & ((1ull << numOut) - 1)) == 0)
        {
            const auto fixed = outputChannel.load (std::memory_order_relaxed);

            if (fixed >= 0)
                outputMask.store (1ull << fixed, std::memory_order_relaxed);
        }
    }

    // 鳴っている最中にデバイスが開き直ったら、フェードインからやり直す（急な大音量を避ける）。
    // start コマンドは running 状態では受け付けられないため、いったん fadeOut の
    // 位置0へ落としてから start を積む（issue #20）。コールバックはここでは止まっている
    //（デバイス再オープン中）ので、状態を直接書いて安全
    if (isNoiseAudible())
    {
        noiseState.store (NoiseState::fadeOut, std::memory_order_relaxed);
        fadePosition.store (0, std::memory_order_relaxed);
        noiseCommand.store (NoiseCommand::start, std::memory_order_relaxed);
    }
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    stopNoise();
    measurement.stopMeasurement();

    eeq::log::write ("audio device error: " + errorMessage + " (noise and measurement stopped)");

    {
        const juce::ScopedLock lock (errorMutex);
        pendingErrorMessage = errorMessage;
    }
    hasPendingError.store (true, std::memory_order_relaxed);
}

//==============================================================================
// 起動時のデバイスオープン

void AudioEngine::openInitialDevice()
{
    juce::OwnedArray<juce::AudioIODeviceType> types;
    deviceManager.createAudioDeviceTypes (types);

    if (types.isEmpty())
        return;

    // デバイスタイプの優先順位（仕様 6.1）:
    //   Windows は ASIO を最優先、なければ WASAPI（共有 "Windows Audio"）へフォールバック。
    //   Mac は CoreAudio。どれも使えなければ先頭の型
    auto hasDevices = [] (juce::AudioIODeviceType* type)
    {
        type->scanForDevices();
        return type->getDeviceNames (true).size() + type->getDeviceNames (false).size() > 0;
    };

    auto findType = [&types, &hasDevices] (bool exact, const juce::String& name)
    {
        for (auto* type : types)
        {
            const auto matches = exact ? type->getTypeName() == name
                                       : type->getTypeName().containsIgnoreCase (name);

            if (matches && hasDevices (type))
                return type;
        }

        return (juce::AudioIODeviceType*) nullptr;
    };

    juce::AudioIODeviceType* chosen = nullptr;

   #if JUCE_WINDOWS
    chosen = findType (false, "ASIO");           // 仕様 6.1: ASIO を優先
    if (chosen == nullptr)
        chosen = findType (true, "Windows Audio"); // WASAPI 共有
    if (chosen == nullptr)
        chosen = findType (false, "Windows Audio"); // WASAPI の他のモード
   #elif JUCE_MAC
    chosen = findType (true, "CoreAudio");
   #endif

    if (chosen == nullptr && hasDevices (types[0]))
        chosen = types[0];

    if (chosen == nullptr)
    {
        eeq::log::write ("audio open FAILED: no usable device type");
        return;
    }

    deviceManager.setCurrentAudioDeviceType (chosen->getTypeName(), true);

    // 起動直後はまだ deviceManager がデバイス型を持っていない（initialise 内のスキャンで
    // 作られる）ため、ローカルの types で列挙する
    auto* typeForEnumeration = chosen;
    typeForEnumeration->scanForDevices(); // 遅延スキャンを明示的に起こす

    eeq::log::write ("device type: " + typeForEnumeration->getTypeName()
                     + " / input devices: [" + typeForEnumeration->getDeviceNames (true).joinIntoString ("; ")
                     + "] / output devices: [" + typeForEnumeration->getDeviceNames (false).joinIntoString ("; ") + "]");

    auto setup = deviceManager.getAudioDeviceSetup();

    // デバイス名が未指定のときは、入力/出力それぞれの既定デバイスを明示的に埋める。
    // （名前が空のまま setAudioDeviceSetup に渡すと、JUCE はエラー無しで何も開かない）
    if (setup.inputDeviceName.isEmpty())
    {
        const auto names = typeForEnumeration->getDeviceNames (true);

        if (! names.isEmpty())
            setup.inputDeviceName = names[typeForEnumeration->getDefaultDeviceIndex (true)];
    }

    if (setup.outputDeviceName.isEmpty())
    {
        const auto names = typeForEnumeration->getDeviceNames (false);

        if (! names.isEmpty())
            setup.outputDeviceName = names[typeForEnumeration->getDefaultDeviceIndex (false)];
    }
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 64, true);
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 64, true);

    if (settings != nullptr)
    {
        const auto savedInputName = settings->getValue ("inputDeviceName");
        const auto savedOutputName = settings->getValue ("outputDeviceName");

        if (savedInputName.isNotEmpty())
            setup.inputDeviceName = savedInputName;

        if (savedOutputName.isNotEmpty())
            setup.outputDeviceName = savedOutputName;

        const auto savedRate = settings->getDoubleValue ("sampleRate", 0.0);

        if (savedRate > 0.0)
            setup.sampleRate = savedRate;

        const auto savedBuffer = settings->getIntValue ("bufferSize", 0);

        if (savedBuffer > 0)
            setup.bufferSize = savedBuffer;
    }

    auto error = deviceManager.initialise (0, 0, nullptr, true, {}, &setup);

    eeq::log::write ("initialise result: \"" + error + "\" setup: input=\"" + setup.inputDeviceName
                     + "\" output=\"" + setup.outputDeviceName + "\"");

    if (error.isNotEmpty())
    {
        // フォールバック: 保存したレート/バッファを捨てて既定の構成で再試行
        eeq::log::write ("initial open FAILED: " + error + " (retrying with default settings)");
        auto fallback = deviceManager.getAudioDeviceSetup();
        fallback.useDefaultInputChannels = true;
        fallback.useDefaultOutputChannels = true;
        fallback.sampleRate = 0.0;
        fallback.bufferSize = 0;
        deviceManager.initialise (0, 0, nullptr, true, {}, &fallback);
    }

    int numInputs = 0, numOutputs = 0;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        numInputs = device->getActiveInputChannels().countNumberOfSetBits();
        numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();

        const auto setupNow = deviceManager.getAudioDeviceSetup();
        eeq::log::write ("audio opened: input=\"" + (setupNow.inputDeviceName.isNotEmpty() ? setupNow.inputDeviceName : juce::String (u8"(既定)"))
                         + "\" output=\"" + (setupNow.outputDeviceName.isNotEmpty() ? setupNow.outputDeviceName : juce::String (u8"(既定)"))
                         + "\" rate=" + juce::String (device->getCurrentSampleRate(), 0)
                         + " buffer=" + juce::String (device->getCurrentBufferSizeSamples())
                         + " in=" + juce::String (numInputs) + "ch"
                         + " out=" + juce::String (numOutputs) + "ch");
    }
    else
    {
        eeq::log::write ("audio open FAILED: no device");
    }

    // 既定のサンプルレートは 48 kHz（仕様 2.）。保存された設定がなければ切り替える
    if (settings != nullptr && settings->getDoubleValue ("sampleRate", 0.0) == 0.0)
    {
        if (auto* device = deviceManager.getCurrentAudioDevice())
        {
            if (device->getAvailableSampleRates().contains (48000.0)
                && std::abs (device->getCurrentSampleRate() - 48000.0) > 0.5)
                applyRateAndBuffer (48000.0, -1);
        }
    }

    restoreSavedAssignment (numInputs, numOutputs);
}

void AudioEngine::restoreSavedAssignment (int numInputs, int numOutputs)
{
    const auto clamp = [] (int saved, int available, int fallback)
    { return saved >= 0 && saved < available ? saved : fallback; };

    const auto savedRef = settings != nullptr ? settings->getIntValue ("refChannel", 1) : 1;
    const auto savedMeas = settings != nullptr ? settings->getIntValue ("measChannel", 0) : 0;
    const auto savedOut = settings != nullptr ? settings->getIntValue ("outputChannel", 0) : 0;

    if (savedRef == internalRefChannel)
    {
        refChannel.store (internalRefChannel, std::memory_order_relaxed);
    }
    else
    {
        // 入力が1chしかないときは Ref を内部参照にする（マイクを Meas に残すため）
        refChannel.store (clamp (savedRef, numInputs, numInputs > 1 ? 1 : internalRefChannel),
                          std::memory_order_relaxed);
    }
    measChannel.store (clamp (savedMeas, numInputs, numInputs > 0 ? 0 : -1), std::memory_order_relaxed);
    outputChannel.store (clamp (savedOut, numOutputs, 0), std::memory_order_relaxed);

    // 系統ルーティング。保存があれば復元し、なければ出力1chだけ
    if (settings != nullptr)
    {
        const auto savedMask = (uint64_t) settings->getValue ("outputMask",
                                                              juce::String (1ull << outputChannel.load (std::memory_order_relaxed)))
                                       .getLargeIntValue();

        if (savedMask != 0)
            outputMask.store (savedMask, std::memory_order_relaxed);

        const auto savedCeiling = (float) settings->getDoubleValue ("outputCeilingDb", 0.0);
        outputCeilingDb.store (juce::jlimit (-60.0f, 0.0f, savedCeiling), std::memory_order_relaxed);
    }

    if (settings != nullptr)
    {
        const auto savedLevel = (float) settings->getDoubleValue ("noiseLevelDb", defaultNoiseLevelDb);
        noiseLevelDb.store (juce::jlimit (-60.0f, 0.0f, savedLevel), std::memory_order_relaxed);
    }
}

} // namespace eeq::audio
