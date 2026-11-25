#include "AudioEngine.h"

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
    deviceManager.addAudioCallback (this);
    openInitialDevice();
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

    if (error.isEmpty())
        saveSettings();

    return error;
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
// ピンクノイズ

void AudioEngine::startNoise() noexcept
{
    fadePosition.store (0, std::memory_order_relaxed);
    noiseState.store (NoiseState::fadeIn, std::memory_order_relaxed);
}

void AudioEngine::stopNoise() noexcept
{
    const auto state = noiseState.load (std::memory_order_relaxed);

    if (state == NoiseState::running || state == NoiseState::fadeIn)
    {
        fadePosition.store (0, std::memory_order_relaxed);
        noiseState.store (NoiseState::fadeOut, std::memory_order_relaxed);
    }
}

bool AudioEngine::isNoiseAudible() const noexcept
{
    const auto state = noiseState.load (std::memory_order_relaxed);
    return state == NoiseState::fadeIn || state == NoiseState::running;
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
    settings->setValue ("noiseLevelDb", (double) noiseLevelDb.load (std::memory_order_relaxed));
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
    const auto outIndex = outputChannel.load (std::memory_order_relaxed);
    const auto refIsInternal = refIndex == internalRefChannel;

    measureChannel (inputChannelData, numInputChannels, numSamples, refIndex, refMeter, clipThresholdLinear);
    measureChannel (inputChannelData, numInputChannels, numSamples, measIndex, measMeter, clipThresholdLinear);

    const auto level = juce::Decibels::decibelsToGain (noiseLevelDb.load (std::memory_order_relaxed));
    auto state = noiseState.load (std::memory_order_relaxed);
    auto position = fadePosition.load (std::memory_order_relaxed);

    const float* refIn = (refIndex >= 0 && refIndex < numInputChannels) ? inputChannelData[refIndex] : nullptr;
    const float* measIn = (measIndex >= 0 && measIndex < numInputChannels) ? inputChannelData[measIndex] : nullptr;
    const auto streamCount = juce::jmin (numSamples, (int) refStream.size());

    float outPeak = 0.0f;
    double outSumSquares = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        float gain = 0.0f;

        if (state != NoiseState::idle)
        {
            const auto isFadeIn = state == NoiseState::fadeIn;
            const auto length = juce::jmax (1, isFadeIn ? fadeInLength.load (std::memory_order_relaxed)
                                                        : fadeOutLength.load (std::memory_order_relaxed));
            const auto t = (float) position / (float) length;

            gain = level * (isFadeIn ? t : 1.0f - t);

            if (++position >= length)
            {
                position = 0;
                state = isFadeIn ? NoiseState::running : NoiseState::idle;
            }
        }

        const auto sample = noise.nextSample() * gain;

        outPeak = juce::jmax (outPeak, std::abs (sample));
        outSumSquares += (double) sample * (double) sample;

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (auto* out = outputChannelData[ch])
                out[i] = (ch == outIndex) ? sample : 0.0f;

        // 測定ストリーム（仕様 2. の (1)）: Ref/Meas のペアをリングバッファへ書く
        if (i < streamCount)
        {
            refStream[(size_t) i] = refIsInternal ? sample : (refIn != nullptr ? refIn[i] : 0.0f);
            measStream[(size_t) i] = (measIn != nullptr ? measIn[i] : 0.0f);
        }
    }

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

    // デバイスを開き直したときは、必ずフェードインから再開する（急な大音量を避ける）
    const auto state = noiseState.load (std::memory_order_relaxed);

    if (state == NoiseState::running || state == NoiseState::fadeOut)
    {
        fadePosition.store (0, std::memory_order_relaxed);
        noiseState.store (NoiseState::fadeIn, std::memory_order_relaxed);
    }
}

void AudioEngine::audioDeviceStopped() {}

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    stopNoise();
    measurement.stopMeasurement();

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
    // デバイスタイプ（Mac: CoreAudio。Windows の ASIO/WASAPI はフェーズ6で追加）
    juce::OwnedArray<juce::AudioIODeviceType> types;
    deviceManager.createAudioDeviceTypes (types);

    if (types.isEmpty())
        return;

    deviceManager.setCurrentAudioDeviceType (types[0]->getTypeName(), true);

    auto setup = deviceManager.getAudioDeviceSetup();
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

    if (error.isNotEmpty())
    {
        // フォールバック: 既定のチャンネル構成で再試行
        auto fallback = deviceManager.getAudioDeviceSetup();
        fallback.useDefaultInputChannels = true;
        fallback.useDefaultOutputChannels = true;
        deviceManager.initialise (0, 0, nullptr, true, {}, &fallback);
    }

    int numInputs = 0, numOutputs = 0;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        numInputs = device->getActiveInputChannels().countNumberOfSetBits();
        numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
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

    if (settings != nullptr)
    {
        const auto savedLevel = (float) settings->getDoubleValue ("noiseLevelDb", defaultNoiseLevelDb);
        noiseLevelDb.store (juce::jlimit (-60.0f, 0.0f, savedLevel), std::memory_order_relaxed);
    }
}

} // namespace eeq::audio
