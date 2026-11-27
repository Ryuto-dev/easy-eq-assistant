#include <gtest/gtest.h>

#include "DSP/MeasurementEngine.h"

#include <array>
#include <chrono>
#include <thread>

TEST (MeasurementEngine, StartRequestEventuallyAccumulatesFrames)
{
    eeq::dsp::SampleRingBuffer ring (32768);
    eeq::dsp::MeasurementEngine engine (ring);
    engine.configure (256, 48000.0,
                      { eeq::dsp::AveragingConfig::Mode::cumulative, 256 },
                      juce::dsp::WindowingFunction<float>::hann);

    engine.startMeasurement();
    EXPECT_TRUE (engine.isMeasuring());

    std::array<float, 256> samples {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (2);

    while (engine.getAverages() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        ring.write (samples.data(), samples.data(), (int) samples.size());
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    EXPECT_GT (engine.getAverages(), 0);
    engine.stopMeasurement();
    EXPECT_FALSE (engine.isMeasuring());
}
