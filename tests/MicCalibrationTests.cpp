#include <gtest/gtest.h>

#include "Core/MicCalibration.h"

#include <cmath>

// マイク補正ファイル（ロードマップ B-5）
TEST (MicCalibration, ParsesMiniDspFileWithSensitivityLine)
{
    const juce::String text = "\"Sens Factor =-1.23dB, SERNO: 7012345\"\n"
                              "10.054\t-4.32\n"
                              "100\t0.10\n"
                              "1000\t0\n"
                              "10000\t2.5\n"
                              "20000\t-3.0\n";
    juce::String error;
    const auto cal = eeq::core::MicCalibration::parse (text, "umik.txt", error);

    ASSERT_NE (cal, nullptr) << error;
    EXPECT_TRUE (cal->hasSensitivity);
    EXPECT_NEAR (cal->sensitivityDb, -1.23, 1.0e-9);
    EXPECT_EQ (cal->freqHz.size(), 5u);
    EXPECT_TRUE (cal->phaseDeg.empty());
    EXPECT_NEAR (cal->dbAt (1000.0), 0.0, 1.0e-9);
    // 1 kHz と 10 kHz の対数中点（3162 Hz）は 1.25 dB
    EXPECT_NEAR (cal->dbAt (std::sqrt (1000.0 * 10000.0)), 1.25, 1.0e-6);
    // 範囲外は端の値
    EXPECT_NEAR (cal->dbAt (5.0), -4.32, 1.0e-9);
    EXPECT_NEAR (cal->dbAt (30000.0), -3.0, 1.0e-9);
}

TEST (MicCalibration, ParsesFrdWithPhaseAndComments)
{
    const juce::String text = "* Dayton Audio calibration\n* Freq(Hz) SPL(dB) Phase(deg)\n"
                              "20, -1.0, 5.0\n1000, 0.0, 0.0\n20000, 1.0, -10.0\n";
    juce::String error;
    const auto cal = eeq::core::MicCalibration::parse (text, "mic.frd", error);

    ASSERT_NE (cal, nullptr) << error;
    EXPECT_EQ (cal->phaseDeg.size(), 3u);

    double db = 0.0, phase = 0.0;
    cal->valueAt (20000.0, db, phase);
    EXPECT_NEAR (db, 1.0, 1.0e-9);
    EXPECT_NEAR (phase, -10.0, 1.0e-9);
}

TEST (MicCalibration, RejectsTextWithoutData)
{
    juce::String error;
    EXPECT_EQ (eeq::core::MicCalibration::parse ("hello\nworld\n", "x.txt", error), nullptr);
    EXPECT_FALSE (error.isEmpty());
}

TEST (MicCalibration, ApplyRemovesTheMicResponseFromMeasAndTransfer)
{
    // マイクが 2 kHz 以上で +3 dB 持ち上がる → 補正後は平ら
    const juce::String text = "20 0\n1999 0\n2000 3\n20000 3\n";
    juce::String error;
    const auto cal = eeq::core::MicCalibration::parse (text, "m.txt", error);
    ASSERT_NE (cal, nullptr);

    eeq::dsp::SpectrumFrame frame;
    frame.fftSize = 1024;
    frame.sampleRate = 48000.0;
    frame.averages = 4;
    const auto bins = (size_t) frame.fftSize / 2 + 1;
    const auto binHz = frame.sampleRate / frame.fftSize;

    for (size_t k = 0; k < bins; ++k)
    {
        const auto micDb = (double) k * binHz >= 2000.0 ? 3.0 : 0.0;
        const auto gain = (float) std::pow (10.0, micDb / 20.0);
        frame.transfer.push_back ({ gain, 0.0f });
        frame.magnitudeDb.push_back ((float) micDb);
        frame.phaseDeg.push_back (0.0f);
        frame.measPowerDb.push_back ((float) (-20.0 + micDb));
        frame.refPowerDb.push_back (-20.0f);
        frame.coherence.push_back (1.0f);
    }

    cal->applyTo (frame);

    for (size_t k = 1; k < bins; ++k)
    {
        EXPECT_NEAR (std::abs (std::complex<float> (frame.transfer[k])), 1.0f, 1.0e-4f) << "bin " << k;
        EXPECT_NEAR (frame.magnitudeDb[k], 0.0f, 1.0e-4f);
        EXPECT_NEAR (frame.measPowerDb[k], -20.0f, 1.0e-4f);
        EXPECT_NEAR (frame.refPowerDb[k], -20.0f, 1.0e-6f); // Ref は触らない
    }
}
