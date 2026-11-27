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
    // ログは呼び出し側（UI）が取る（issue #57: エンジンの public メソッドを RT 安全に保つ）
    uiWatchdogStopPending.store (false, std::memory_order_relaxed);
    const auto deadline = audioSampleCounter.load (std::memory_order_relaxed)
                        + uiWatchdogTimeoutSamples.load (std::memory_order_relaxed);
    excitationRequest.store ((deadline << 1) | pendingStartBit, std::memory_order_release);
}

void AudioEngine::stopExcitation() noexcept
{
    excitationRequest.store (0, std::memory_order_release);
}

void AudioEngine::kickUiWatchdog() noexcept
{
    auto request = excitationRequest.load (std::memory_order_acquire);

    while (request != 0)
    {
        const auto deadline = audioSampleCounter.load (std::memory_order_relaxed)
                            + uiWatchdogTimeoutSamples.load (std::memory_order_relaxed);
        const auto updated = (deadline << 1) | (request & pendingStartBit);

        if (excitationRequest.compare_exchange_weak (request, updated, std::memory_order_acq_rel))
            return;
    }
}

void AudioEngine::emergencyStop() noexcept
{
    eeq::log::write ("EMERGENCY STOP (Esc): stopping output and measurement");
    stopExcitation();
    measurement.stopMeasurement();
}

bool AudioEngine::isExcitationAudible() const noexcept
{
    const auto state = noiseState.load (std::memory_order_relaxed);
    return state == NoiseState::fadeIn || state == NoiseState::running;
}

void AudioEngine::publishExcitationBuffer (std::vector<float> samples)
{
    // 旧バッファの寿命（issue #33 / #54 / #94）:
    //  - 直近3世代（現行＋旧2世代）を常に保持する。オーディオスレッドがブロック先頭で
    //    activeBufferPtr を読んだ直後に差し替わっても、読んだ世代が生きている
    //  - さらに、オーディオスレッドが「実際に鳴らしている」世代（adoptedBufferPtr）は、
    //    3世代より古くなっても解放しない。構成切替（issue #104）では鳴っている間は
    //    新しいバッファへ移らないため、その間に何度差し替えても古い世代を読み続けられる
    if (ownedBuffers[2] != nullptr)
        retiredBuffers.push_back (std::move (ownedBuffers[2]));

    ownedBuffers[2] = std::move (ownedBuffers[1]);
    ownedBuffers[1] = std::move (ownedBuffers[0]);
    ownedBuffers[0] = std::make_shared<const std::vector<float>> (std::move (samples));
    activeBufferPtr.store (ownedBuffers[0].get(), std::memory_order_release);

    const auto* adopted = adoptedBufferPtr.load (std::memory_order_acquire);
    retiredBuffers.erase (std::remove_if (retiredBuffers.begin(), retiredBuffers.end(),
                                          [adopted] (const auto& buffer) { return buffer.get() != adopted; }),
                          retiredBuffers.end());
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

    const auto fileRate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const auto deviceRate = currentSampleRate.load() > 0.0 ? currentSampleRate.load() : 48000.0;

    // メモリ保護（issue #105）: 10 分まで、かつ読み込みバッファ（全ch）を 64M サンプル（256 MB）以内にする。
    // 以前は 20 分を全chそのまま読んでいたため、192 kHz ステレオで約 2.7 GB になり落ちていた
    const auto channels = juce::jmax (1, (int) reader->numChannels);
    const auto maxByDuration = (juce::int64) (10.0 * 60.0 * fileRate);
    const auto maxByMemory = (juce::int64) (64 * 1024 * 1024) / channels;
    const auto length = juce::jmin (reader->lengthInSamples, maxByDuration, maxByMemory);

    if (length <= 0)
    {
        errorOut = juce::String (u8"空のファイルです: ") + file.getFullPathName();
        return false;
    }

    std::vector<float> mono;

    try
    {
        juce::AudioBuffer<float> buffer (channels, (int) length);
        reader->read (&buffer, 0, (int) length, 0, true, true);

        // 励起信号はモノラルで十分（系統の1chに出す）。ステレオは平均する。非有限値は 0 にする
        std::vector<float> fileMono ((size_t) length, 0.0f);

        for (int i = 0; i < (int) length; ++i)
        {
            float sum = 0.0f;

            for (int ch = 0; ch < channels; ++ch)
                sum += buffer.getSample (ch, i);

            const auto value = sum / (float) channels;
            fileMono[(size_t) i] = std::isfinite (value) ? value : 0.0f;
        }

        // デバイスのサンプルレートへ変換する（issue #105: 44.1 kHz のファイルを 48 kHz で鳴らすと 9% 速くなっていた）
        if (std::abs (fileRate - deviceRate) > 0.5)
        {
            const auto ratio = fileRate / deviceRate; // 入力サンプル / 出力サンプル
            const auto outLength = (int) std::floor ((double) length / ratio);
            mono.assign ((size_t) juce::jmax (0, outLength), 0.0f);
            juce::WindowedSincInterpolator interpolator;
            interpolator.process (ratio, fileMono.data(), mono.data(), (int) mono.size(),
                                  (int) fileMono.size(), 0);
        }
        else
        {
            mono = std::move (fileMono);
        }
    }
    catch (const std::bad_alloc&)
    {
        errorOut = juce::String (u8"ファイルが大きすぎて読み込めません: ") + file.getFullPathName();
        eeq::log::write ("excitation: WAV load FAILED (out of memory): " + file.getFullPathName());
        return false;
    }

    // WAVはピーク1.0へ正規化して保持する（issue #29）。レベル設定（dBFS）は
    // オーディオスレッドでピーク基準として掛かるので、どのファイルでも
    // 「設定した dBFS ＝ ピーク」が一致する
    float peak = 0.0f;

    for (const auto value : mono)
        peak = juce::jmax (peak, std::abs (value));

    if (peak > 1.0e-9f)
        for (auto& value : mono)
            value /= peak;

    stopExcitation();
    eeq::log::write ("excitation: WAV loaded \"" + file.getFullPathName() + "\" ("
                     + juce::String ((double) length / fileRate, 2) + " s, " + juce::String (fileRate, 0) + " Hz -> "
                     + juce::String (deviceRate, 0) + " Hz"
                     + (length < reader->lengthInSamples ? ", truncated to 10 min / memory limit" : "") + ")");

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
    startExcitation();
    eeq::log::write ("calibration signal: started");
}

//==============================================================================
// デバイスエラー

bool AudioEngine::consumeFallbackNotice (juce::String& messageOut)
{
    if (! hasFallbackNotice.exchange (false, std::memory_order_acq_rel))
        return false;

    const juce::ScopedLock lock (errorMutex);
    messageOut = fallbackNotice;
    return true;
}

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
    // Compare with the start of the block being rendered. The UI sees the end
    // of the submitted block; counting that block again could expire a fresh heartbeat.
    const auto blockStart = audioSampleCounter.fetch_add ((std::uint64_t) juce::jmax (0, numSamples),
                                                          std::memory_order_relaxed);
    auto request = excitationRequest.load (std::memory_order_acquire);
    auto command = NoiseCommand::none;

    if (request == 0)
    {
        command = NoiseCommand::stop;
    }
    else if (blockStart >= (request >> 1))
    {
        // Only expire the request we examined, never a newly published start or heartbeat.
        if (excitationRequest.compare_exchange_strong (request, 0, std::memory_order_acq_rel))
        {
            uiWatchdogStopPending.store (true, std::memory_order_release);
            measurement.stopMeasurement();
            command = NoiseCommand::stop;
        }
    }
    else if ((request & pendingStartBit) != 0
             && excitationRequest.compare_exchange_strong (request, request & ~pendingStartBit,
                                                           std::memory_order_acq_rel))
    {
        command = NoiseCommand::start;
    }

    const auto refIndex = refChannel.load (std::memory_order_relaxed);
    const auto measIndex = measChannel.load (std::memory_order_relaxed);
    const auto refIsInternal = refIndex == internalRefChannel;

    measureChannel (inputChannelData, numInputChannels, numSamples, refIndex, refMeter, clipThresholdLinear);
    measureChannel (inputChannelData, numInputChannels, numSamples, measIndex, measMeter, clipThresholdLinear);

    const auto level = juce::Decibels::decibelsToGain (noiseLevelDb.load (std::memory_order_relaxed));
    auto state = noiseState.load (std::memory_order_relaxed);
    auto position = fadePosition.load (std::memory_order_relaxed);
    auto playPos = playbackPosition.load (std::memory_order_relaxed);

    // 校正信号（1kHz帯域制限ノイズ）のフィルタ係数。サンプルレートが変わったら作り直す
    //（オーディオスレッドだけが触る状態なのでロックは不要）
    if ((appliedSource == ExcitationSource::bandNoise1k
         || excitationSource.load (std::memory_order_relaxed) == ExcitationSource::bandNoise1k)
        && bandNoiseCoeffsRate != currentSampleRate.load())
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

    // ---- 出力構成（ソース・ルーティング・スイープ/WAVバッファ）の切替（issue #104）----
    // 音が出ている間に要求が変わったら、今の構成のまま短くフェードアウトし、無音になってから
    // 新しい構成に切り替えて通常のフェードインで再開する（段差的に別の信号・別のchへ飛ばない）
    {
        const auto requestedSource = excitationSource.load (std::memory_order_relaxed);
        const auto requestedMask = outputMask.load (std::memory_order_relaxed);
        const auto* requestedBuffer = activeBufferPtr.load (std::memory_order_acquire);
        const bool changed = requestedSource != appliedSource || requestedMask != appliedMask
                          || requestedBuffer != appliedBuffer;

        if (state == NoiseState::idle)
        {
            appliedSource = requestedSource;
            appliedMask = requestedMask;
            appliedBuffer = requestedBuffer;
            adoptedBufferPtr.store (appliedBuffer, std::memory_order_release);
            switchPending = false;

            // 構成切替のフェードアウトが終わった: まだ鳴らす要求があれば新しい構成でフェードインする
            if (pendingResumeAfterSwitch)
            {
                pendingResumeAfterSwitch = false;

                if (command != NoiseCommand::stop)
                {
                    fadeStartEnvelope.store (0.0f, std::memory_order_relaxed);
                    state = NoiseState::fadeIn;
                    position = 0;
                    playPos = 0;
                    playbackPosition.store (0, std::memory_order_relaxed);
                }
            }
        }
        else if (changed && ! switchPending && (state == NoiseState::fadeIn || state == NoiseState::running))
        {
            if (state == NoiseState::fadeIn)
            {
                const auto length = juce::jmax (1, fadeInLength.load (std::memory_order_relaxed));
                fadeStartEnvelope.store (fadeEnvelopeAt (1, position, length, fadeStartEnvelope.load (std::memory_order_relaxed)),
                                         std::memory_order_relaxed);
            }
            else
            {
                fadeStartEnvelope.store (1.0f, std::memory_order_relaxed);
            }

            state = NoiseState::fadeOut;
            position = 0;
            switchPending = true;
        }
    }

    const auto source = appliedSource;
    const auto rmsNormalise = rmsNormaliseFor (source); // issue #51: 全ソースで RMS を設定値に揃える
    const auto routingMask = appliedMask;
    const auto switchFadeLength = juce::jmax (1, (int) (switchFadeSeconds * currentSampleRate.load (std::memory_order_relaxed)));

    // オーディオスレッドで使う値はブロックの先頭で一度だけ読む（リアルタイム性）。
    // バッファは atomic な生ポインタを acquire 読みするだけで、ロックは行わない（issue #33）
    const auto* activeSweep = (source == ExcitationSource::sweep || source == ExcitationSource::wavFile)
                                ? appliedBuffer
                                : nullptr;

    // UIからの開始/停止コマンドをブロックの先頭で反映する（状態はオーディオスレッドが所有する）。
    // フェードは「開始時点の包絡」を引き継ぐ（issue #53: フェードイン中の停止で
    // 0.2 → 1.0 のようにフルレベルへ跳ねてからフェードアウトするのを防ぐ）
    switch (command)
    {
        case NoiseCommand::start:
            // 構成切替のフェードアウト中は、無音になったところで自動的に再開する
            if (! switchPending && (state == NoiseState::idle || state == NoiseState::fadeOut))
            {
                if (state == NoiseState::fadeOut)
                {
                    // フェードアウト途中からの再開: 現在の包絡からフェードインを続ける
                    const auto length = juce::jmax (1, fadeOutLength.load (std::memory_order_relaxed));
                    fadeStartEnvelope.store (
                        fadeEnvelopeAt (3, position, length, fadeStartEnvelope.load (std::memory_order_relaxed)),
                        std::memory_order_relaxed);
                }
                else
                {
                    fadeStartEnvelope.store (0.0f, std::memory_order_relaxed);
                }

                state = NoiseState::fadeIn;
                position = 0;
                playPos = 0;
                playbackPosition.store (0, std::memory_order_relaxed);
            }
            break;

        case NoiseCommand::stop:
            switchPending = false; // 停止要求が来たら切替後の再開はしない

            if (state == NoiseState::running || state == NoiseState::fadeIn)
            {
                if (state == NoiseState::fadeIn)
                {
                    const auto length = juce::jmax (1, fadeInLength.load (std::memory_order_relaxed));
                    fadeStartEnvelope.store (
                        fadeEnvelopeAt (1, position, length, fadeStartEnvelope.load (std::memory_order_relaxed)),
                        std::memory_order_relaxed);
                }
                else
                {
                    fadeStartEnvelope.store (1.0f, std::memory_order_relaxed);
                }

                state = NoiseState::fadeOut;
                position = 0;
            }
            break;

        case NoiseCommand::none:
            break;
    }

    const auto ceilingGain = juce::Decibels::decibelsToGain (outputCeilingDb.load (std::memory_order_relaxed));

    const float* refIn = (refIndex >= 0 && refIndex < numInputChannels) ? inputChannelData[refIndex] : nullptr;
    const float* measIn = (measIndex >= 0 && measIndex < numInputChannels) ? inputChannelData[measIndex] : nullptr;
    const auto streamCount = juce::jmin (numSamples, (int) refStream.size());

    float outPeak = 0.0f;
    double outSumSquares = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // フェード包絡（0..1）。レベル設定はブロック先頭の level、ソースごとの RMS 正規化は
        // ブロック先頭の rmsNormalise、最後に一括して掛ける（issue #29/#51: レベル定義の統一）
        float envelope = 0.0f;

        if (state == NoiseState::fadeIn)
        {
            const auto length = juce::jmax (1, fadeInLength.load (std::memory_order_relaxed));
            envelope = fadeEnvelopeAt (1, position, length, fadeStartEnvelope.load (std::memory_order_relaxed));

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
            const auto length = switchPending ? switchFadeLength
                                              : juce::jmax (1, fadeOutLength.load (std::memory_order_relaxed));
            envelope = fadeEnvelopeAt (3, position, length, fadeStartEnvelope.load (std::memory_order_relaxed));

            if (++position >= length)
            {
                position = 0;

                if (switchPending)
                {
                    // 無音になったので次のブロックから新しい構成で、0 からフェードインし直す
                    switchPending = false;
                    state = NoiseState::idle;
                    pendingResumeAfterSwitch = true;
                }
                else
                {
                    state = NoiseState::idle;
                    noiseReachedIdle.store (true, std::memory_order_relaxed);
                }
            }
        }

        // 励起信号ソースを選ぶ（仕様 3.2）。各 case は「素の波形」を出し、
        // RMS への正規化は最後の sample 計算で rmsNormalise として一括して掛ける
        float sourceSample = 0.0f;

        switch (source)
        {
            case ExcitationSource::pinkNoise:
                // 生成器が実効RMS≈1.0に正規化済み
                sourceSample = noise.nextSample();
                break;

            case ExcitationSource::sweep:
            case ExcitationSource::wavFile:
            {
                // スイープ: ピーク1.0で生成（実効RMS 0.7071）。WAV: ピーク1.0へ正規化済み。
                // RMS/ピーク基準の違いは rmsNormaliseFor が吸収する
                if (activeSweep != nullptr && playPos < (int) activeSweep->size())
                {
                    sourceSample = (*activeSweep)[(size_t) playPos];
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
                tonePhase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / std::max (8000.0, currentSampleRate.load());

                if (tonePhase > 2.0 * juce::MathConstants<double>::pi)
                    tonePhase -= 2.0 * juce::MathConstants<double>::pi;

                sourceSample = (float) std::sin (tonePhase);
                break;
            }

            case ExcitationSource::bandNoise1k:
            {
                // 比較校正の既定信号: 1kHz中心の帯域制限ピンクノイズ（仕様 3.4）。
                // 生の出力（実測 RMS≈0.314）を出し、RMS 揃えは rmsNormaliseFor が行う
                const auto pink = noise.nextSample();
                const auto b0 = bandNoiseCoeffs[0], b2 = bandNoiseCoeffs[2];
                const auto a1 = bandNoiseCoeffs[3], a2 = bandNoiseCoeffs[4];

                const auto out = b0 * pink + b2 * bandNoiseX2 - a1 * bandNoiseY1 - a2 * bandNoiseY2;

                bandNoiseX2 = bandNoiseX1;
                bandNoiseX1 = pink;
                bandNoiseY2 = bandNoiseY1;
                bandNoiseY1 = out;

                sourceSample = (float) out;
                break;
            }
        }

        // 出力のハード上限（仕様 3.7 の安全設計）。包絡・RMS正規化・レベルを掛けた最終値に適用する
        // 非有限値（壊れた WAV など）は jlimit を素通りするので 0 にする（issue #105）
        const auto raw = sourceSample * rmsNormalise * envelope * level;
        const auto sample = std::isfinite (raw) ? juce::jlimit (-ceilingGain, ceilingGain, raw) : 0.0f;

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
    uiWatchdogTimeoutSamples.store (uiWatchdogTimeoutSamplesFor (
                                        sampleRate, device->getCurrentBufferSizeSamples()),
                                    std::memory_order_release);
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

    // Ref と Meas が同じ物理chになったら（ch 数の少ないデバイスへ切り替えたとき等）、
    // 伝達関数が 0 dB で平らになってしまうので Ref を内部参照へ落とす（issue #125）
    if (const auto r = refChannel.load (std::memory_order_relaxed);
        r != internalRefChannel && r >= 0 && r == measChannel.load (std::memory_order_relaxed))
        refChannel.store (internalRefChannel, std::memory_order_relaxed);

    // 系統ルーティングマスクを新しいデバイスの出力chに合わせる。存在しない ch のビットは落とし、
    // 1つも残らなければ固定したchへ張り直す（issue #126: 64ch でもシフトを未定義動作にしない）
    {
        const auto valid = channelMaskFor (numOut);
        const auto mask = outputMask.load (std::memory_order_relaxed) & valid;

        if (mask != 0)
            outputMask.store (mask, std::memory_order_relaxed);
        else if (const auto fixed = outputChannel.load (std::memory_order_relaxed); fixed >= 0 && fixed < 64)
            outputMask.store (1ull << fixed, std::memory_order_relaxed);
    }

    // デバイスが開き直されても励起は自動で再開しない（issue #102）。
    // audioDeviceStopped() で状態を idle に確定させているので、ここでは鳴り出さない。
    // 再開はユーザーが改めて開始する（抜線・ストール中に押した停止が無視されない）
    appliedSource = excitationSource.load (std::memory_order_relaxed);
    appliedMask = outputMask.load (std::memory_order_relaxed);
    appliedBuffer = activeBufferPtr.load (std::memory_order_acquire);
    adoptedBufferPtr.store (appliedBuffer, std::memory_order_release);
    switchPending = false;
    pendingResumeAfterSwitch = false;
}

void AudioEngine::audioDeviceStopped()
{
    // デバイスが止まった時点で、励起の状態をメッセージスレッド側から確定させる（issue #102）。
    // コールバックは止まっている（AudioDeviceManager の callback lock 下）ので直接書いて安全。
    // running のまま残すと、再オープン時に古い状態から自動再開し、古い包絡から段差で鳴り出していた
    if (isExcitationAudible())
        stoppedByDeviceRestart.store (true, std::memory_order_relaxed);

    stopExcitation();
    noiseState.store (NoiseState::idle, std::memory_order_relaxed);
    fadePosition.store (0, std::memory_order_relaxed);
    fadeStartEnvelope.store (0.0f, std::memory_order_relaxed);
    playbackPosition.store (0, std::memory_order_relaxed);
}

void AudioEngine::fadeOutAndWait (int timeoutMs)
{
    if (! isExcitationAudible())
        return;

    stopExcitation();
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);

    while (isExcitationAudible() || noiseState.load (std::memory_order_relaxed) == NoiseState::fadeOut)
    {
        if (juce::Time::getMillisecondCounter() > deadline || deviceManager.getCurrentAudioDevice() == nullptr)
            break;

        juce::Thread::sleep (5);
    }
}

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    // 任意のスレッド（オーディオ側含む）から呼ばれ得るため、ここではフラグを立てる
    // だけにする（issue #57: エラー処理でのファイルI/O・ロックを撤去）。
    // ログ出力と測定の停止は consumeDeviceError() を呼ぶメッセージスレッド側で行う
    stopExcitation();
    measurement.stopMeasurement(); // atomic フラグのみで RT 安全

    {
        // ロックを長時間持っているスレッドがある場合に備えて TryLock。
        // 取得できなければメッセージは欠落するが、音声スレッドを待たせないことを優先する
        const juce::ScopedTryLock lock (errorMutex);

        if (lock.isLocked())
            pendingErrorMessage = errorMessage;
    }
    hasPendingError.store (true, std::memory_order_release);
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

    if (error.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        // フォールバック（issue #124）: 保存したデバイスが無い（未接続）などで開けなかったら、
        // この型の既定の入出力デバイスを「名前を明示して」開き直す。
        // JUCE は失敗時に currentSetup のデバイス名を消すため、getAudioDeviceSetup() から作ると
        // 名前が空のまま initialise され、エラーも出ずに何も開かない（=無音で起動していた）
        eeq::log::write ("initial open FAILED: " + (error.isNotEmpty() ? error : juce::String ("no device"))
                         + " (falling back to the default devices)");

        juce::AudioDeviceManager::AudioDeviceSetup fallback;
        const auto inputs = typeForEnumeration->getDeviceNames (true);
        const auto outputs = typeForEnumeration->getDeviceNames (false);

        if (! inputs.isEmpty())
            fallback.inputDeviceName = inputs[juce::jmax (0, typeForEnumeration->getDefaultDeviceIndex (true))];

        if (! outputs.isEmpty())
            fallback.outputDeviceName = outputs[juce::jmax (0, typeForEnumeration->getDefaultDeviceIndex (false))];

        fallback.useDefaultInputChannels = false;
        fallback.useDefaultOutputChannels = false;
        fallback.inputChannels.setRange (0, 64, true);
        fallback.outputChannels.setRange (0, 64, true);
        fallback.sampleRate = 0.0;
        fallback.bufferSize = 0;

        const auto fallbackError = deviceManager.initialise (0, 0, nullptr, true, {}, &fallback);

        if (deviceManager.getCurrentAudioDevice() != nullptr)
        {
            const juce::ScopedLock lock (errorMutex);
            fallbackNotice = juce::String (u8"前回のオーディオデバイス（")
                           + (setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : setup.outputDeviceName)
                           + juce::String (u8"）が見つからないため、既定のデバイスで開きました: ")
                           + fallback.inputDeviceName + " / " + fallback.outputDeviceName;
            hasFallbackNotice.store (true, std::memory_order_release);
        }
        else
        {
            eeq::log::write ("fallback open FAILED: " + fallbackError);
            const juce::ScopedLock lock (errorMutex);
            pendingErrorMessage = juce::String (u8"オーディオデバイスを開けませんでした。上部バーで入力・出力を選んでください。")
                                + (fallbackError.isNotEmpty() ? "\n" + fallbackError : juce::String());
            hasPendingError.store (true, std::memory_order_release);
        }
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

        // 保存したマスクは今のデバイスの出力chと照合する（存在しない ch だけを指していると
        // どこからも音が出ないのにメーターは振れる: issue #126）
        if (const auto valid = savedMask & channelMaskFor (numOutputs); valid != 0)
            outputMask.store (valid, std::memory_order_relaxed);
        else
            outputMask.store (1ull << juce::jlimit (0, 63, outputChannel.load (std::memory_order_relaxed)),
                              std::memory_order_relaxed);

        const auto savedCeiling = (float) settings->getDoubleValue ("outputCeilingDb", 0.0);
        outputCeilingDb.store (juce::jlimit (-60.0f, 0.0f, savedCeiling), std::memory_order_relaxed);
    }

    if (settings != nullptr)
    {
        // 起動時は確認ダイアログの上限（−20 dBFS）を超えて復元しない（issue #100:
        // 上限測定などで上げたレベルが保存され、次回起動後に確認なしで大音量が出ていた）
        const auto savedLevel = (float) settings->getDoubleValue ("noiseLevelDb", defaultNoiseLevelDb);
        noiseLevelDb.store (juce::jlimit (-60.0f, outputLevelLimitDb, std::isfinite (savedLevel) ? savedLevel : defaultNoiseLevelDb),
                            std::memory_order_relaxed);
    }
}

} // namespace eeq::audio
