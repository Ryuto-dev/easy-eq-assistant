#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_dsp/juce_dsp.h>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

#include "Audio/AudioEngine.h"
#include "DSP/DelayEstimator.h"

// 実機の音響測定（EEQ_HARDWARE_TESTS=1 のときだけ）。スピーカーからピンクノイズを出して
// マイクで拾い、アプリと同じ経路（AudioEngine → MeasurementEngine）で伝達関数を測る。
//   EEQ_HW_INPUT  : 入力デバイス名（既定 "BMG-22M"）
//   EEQ_HW_OUTPUT : 出力デバイス名（既定 "MacBook Proのスピーカー"）
//   EEQ_HW_MEAS_CH: マイクの入力ch（0 始まり、既定 0）
// Ref は内部参照（アプリの出力信号）。−20 dBFS のピンクノイズが約 14 秒鳴る。
namespace
{
void pump (int ms, eeq::audio::AudioEngine* engine = nullptr)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

    while (juce::Time::getMillisecondCounter() < end)
    {
       #if JUCE_MAC
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false);
       #else
        juce::Thread::sleep (20);
       #endif

        if (engine != nullptr)
            engine->kickUiWatchdog(); // UI の 30Hz タイマーの代わり
    }
}

juce::String envOr (const char* name, const juce::String& fallback)
{
    const auto* value = std::getenv (name);
    return value != nullptr ? juce::String::fromUTF8 (value) : fallback;
}

struct BandSummary
{
    double coherence = 0.0;
    double magnitudeDb = 0.0;
};

BandSummary summarise (const eeq::dsp::SpectrumFrame& frame, double lowHz, double highHz)
{
    BandSummary s;
    int count = 0;
    double power = 0.0;
    const auto binHz = frame.sampleRate / (double) frame.fftSize;

    for (size_t k = 1; k < frame.coherence.size(); ++k)
    {
        const auto f = (double) k * binHz;

        if (f < lowHz || f >= highHz)
            continue;

        s.coherence += frame.coherence[k];
        power += std::pow (10.0, frame.magnitudeDb[k] / 10.0);
        ++count;
    }

    if (count > 0)
    {
        s.coherence /= count;
        s.magnitudeDb = 10.0 * std::log10 (power / count);
    }

    return s;
}

void printFrame (const char* title, const eeq::dsp::SpectrumFrame& frame)
{
    std::printf ("\n[%s] averages=%d fft=%d rate=%.0f refDelay=%.1f samples\n", title, frame.averages,
                 frame.fftSize, frame.sampleRate, frame.refDelaySamples);
    std::printf ("  band(Hz)          |H| dB    coherence\n");

    const double edges[] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

    for (size_t i = 0; i + 1 < std::size (edges); ++i)
    {
        const auto b = summarise (frame, edges[i], edges[i + 1]);
        std::printf ("  %6.0f-%-6.0f  %8.1f    %.3f\n", edges[i], edges[i + 1], b.magnitudeDb, b.coherence);
    }

    const auto mid = summarise (frame, 200.0, 10000.0);
    std::printf ("  200-10k average coherence = %.3f\n", mid.coherence);
}
} // namespace

TEST (LoudspeakerMeasurementHardware, SpeakerToMicTransferFunction)
{
    if (std::getenv ("EEQ_HARDWARE_TESTS") == nullptr)
        GTEST_SKIP() << "set EEQ_HARDWARE_TESTS=1 to run against real audio devices";

    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto inputName = envOr ("EEQ_HW_INPUT", "BMG-22M");
    const auto outputName = envOr ("EEQ_HW_OUTPUT", juce::String::fromUTF8 (u8"MacBook Proのスピーカー"));
    const auto measCh = envOr ("EEQ_HW_MEAS_CH", "0").getIntValue();

    juce::PropertiesFile::Options options;
    options.applicationName = "EasyEQHardwareTest";
    options.filenameSuffix = ".xml";
    options.folderName = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("eeq-hw-test").getFullPathName();
    juce::PropertiesFile settings (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("eeq-hw-test/settings.xml"),
                                   options);
    settings.setValue ("inputDeviceName", inputName);
    settings.setValue ("outputDeviceName", outputName);
    settings.setValue ("sampleRate", 48000.0);
    settings.setValue ("bufferSize", 512);

    eeq::audio::AudioEngine engine (&settings);
    engine.openInitialDevice();
    pump (1500);

    std::printf ("\ninput=\"%s\" output=\"%s\" rate=%.0f buffer=%d meas ch=%d\n",
                 engine.getSelectedInputDeviceName().toRawUTF8(), engine.getSelectedOutputDeviceName().toRawUTF8(),
                 engine.getCurrentSampleRate(), engine.getCurrentBufferSize(), measCh + 1);
    ASSERT_EQ (engine.getSelectedInputDeviceName(), inputName);
    ASSERT_EQ (engine.getSelectedOutputDeviceName(), outputName);

    engine.setRefChannel (eeq::audio::AudioEngine::internalRefChannel);
    engine.setMeasChannel (measCh);
    engine.setOutputRoutingMask (3); // L/R 両方（MacBook のスピーカーはステレオ）
    engine.setExcitationSource (eeq::audio::AudioEngine::ExcitationSource::pinkNoise);
    engine.setNoiseLevelDb (-20.0f);

    auto& measurement = engine.getMeasurement();
    constexpr int fftSize = 16384;
    eeq::dsp::AveragingConfig averaging;
    averaging.mode = eeq::dsp::AveragingConfig::Mode::exponential;
    averaging.maxAverages = 16;
    measurement.configure (fftSize, engine.getCurrentSampleRate(), averaging,
                           juce::dsp::WindowingFunction<float>::hann);
    measurement.setReferenceDelaySamples (0);

    engine.startExcitation();
    pump (1500, &engine); // フェードイン
    measurement.startMeasurement();

    for (int i = 0; i < 12; ++i)
    {
        pump (500, &engine);
        std::printf ("  t=%.1fs drift estimate %.1f ppm (compensating %.1f) averages=%d\n", 0.5 * (i + 1),
                     measurement.getEstimatedDriftPpm(), measurement.getCompensatedDriftPpm(), measurement.getAverages());
    }

    eeq::dsp::SpectrumFrame uncompensated;
    measurement.copyLatestResult (uncompensated);

    // 遅延の検出（アプリの D キーと同じ推定器）
    std::vector<float> ref, meas;
    ASSERT_TRUE (measurement.copyRecentSamples (65536, ref, meas));
    const auto delay = measurement.estimateCurrentDelay (true); // アプリの D キーと同じ経路
    const auto rate = engine.getCurrentSampleRate();
    std::printf ("\ndelay: %.2f samples = %.3f ms (%.2f m at 20 C) confidence=%.1f PSR=%.1f dB valid=%d\n",
                 delay.delaySamples, 1000.0 * delay.delaySamples / rate, 343.2 * delay.delaySamples / rate,
                 delay.confidence, delay.peakToSidelobe, (int) delay.valid);

    double measRms = 0.0;
    for (auto v : meas)
        measRms += (double) v * v;
    measRms = std::sqrt (measRms / (double) meas.size());
    std::printf ("meas level: %.1f dBFS RMS\n", juce::Decibels::gainToDecibels ((float) measRms));

    printFrame ("no delay compensation", uncompensated);

    // 遅延を補償して測り直す（Ref 遅延線: issue #106）
    measurement.setReferenceDelaySamples ((int) std::lround (delay.delaySamples));
    measurement.resetAverages();
    pump (6000, &engine);

    eeq::dsp::SpectrumFrame compensated;
    measurement.copyLatestResult (compensated);
    printFrame ("with delay compensation", compensated);

    // 遅延の時間的な安定性（入出力が別クロックのデバイスだと、ずれ続けたり跳んだりする）
    std::printf ("\ndelay over time (16384-sample windows, every 0.5 s):\n");
    for (int i = 0; i < 12; ++i)
    {
        pump (500, &engine);
        std::vector<float> r, m;
        if (measurement.copyRecentSamples (16384, r, m))
        {
            const auto d = eeq::dsp::estimateDelay (r.data(), m.data(), (int) r.size(), true, 1.0);
            std::printf ("  t=%4.1fs delay=%9.2f samples conf=%5.1f PSR=%5.1f dB\n", 0.5 * (i + 1),
                         d.delaySamples, d.confidence, d.peakToSidelobe);
        }
    }
    std::printf ("status: %s\n", engine.getStatusText().toRawUTF8());
    std::printf ("clock drift: estimated %.1f ppm, compensating %.1f ppm, realignments=%d\n", measurement.getEstimatedDriftPpm(),
                 measurement.getCompensatedDriftPpm(), measurement.getDriftRealignments());

    engine.stopExcitation();
    pump (800, &engine);
    measurement.stopMeasurement();

    // 遅延が検出でき、遅延補償後は（入出力のクロックが違っても）コヒーレンスが高い
    EXPECT_TRUE (delay.valid);
    EXPECT_GE (compensated.averages, 16);
    EXPECT_GE (summarise (compensated, 200.0, 10000.0).coherence, 0.9);
    EXPECT_GE (summarise (compensated, 1000.0, 8000.0).coherence, 0.9);
}
