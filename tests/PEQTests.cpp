#include <gtest/gtest.h>

#include "DSP/PEQ.h"

namespace
{
constexpr double sampleRate = 48000.0;
}

TEST (PEQ, BellGainAtCenterFrequency)
{
    eeq::dsp::peq::Band band;
    band.type = eeq::dsp::peq::Band::Type::bell;
    band.frequency = 1000.0;
    band.gainDb = 6.0;
    band.q = 2.0;

    const auto coeffs = eeq::dsp::peq::coeffsFor (band, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (coeffs, 1000.0, sampleRate), 6.0, 0.05);
}

TEST (PEQ, BellCutAtCenterFrequency)
{
    eeq::dsp::peq::Band band;
    band.type = eeq::dsp::peq::Band::Type::bell;
    band.frequency = 500.0;
    band.gainDb = -6.0;
    band.q = 2.0;

    const auto coeffs = eeq::dsp::peq::coeffsFor (band, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (coeffs, 500.0, sampleRate), -6.0, 0.05);
}

TEST (PEQ, BellIsNearUnityFarAway)
{
    eeq::dsp::peq::Band band;
    band.type = eeq::dsp::peq::Band::Type::bell;
    band.frequency = 1000.0;
    band.gainDb = 6.0;
    band.q = 4.0;

    const auto coeffs = eeq::dsp::peq::coeffsFor (band, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (coeffs, 100.0, sampleRate), 0.0, 0.2);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (coeffs, 10000.0, sampleRate), 0.0, 0.2);
}

TEST (PEQ, ShelvesReachGainInFlatRegion)
{
    eeq::dsp::peq::Band low;
    low.type = eeq::dsp::peq::Band::Type::lowShelf;
    low.frequency = 200.0;
    low.gainDb = 6.0;
    low.q = 0.7071;

    const auto lowCoeffs = eeq::dsp::peq::coeffsFor (low, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (lowCoeffs, 50.0, sampleRate), 6.0, 0.1);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (lowCoeffs, 2000.0, sampleRate), 0.0, 0.2);

    eeq::dsp::peq::Band high;
    high.type = eeq::dsp::peq::Band::Type::highShelf;
    high.frequency = 5000.0;
    high.gainDb = -4.0;
    high.q = 0.7071;

    const auto highCoeffs = eeq::dsp::peq::coeffsFor (high, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (highCoeffs, 12000.0, sampleRate), -4.0, 0.1);
}

TEST (PEQ, PassFiltersBlockAndPass)
{
    eeq::dsp::peq::Band lowPass;
    lowPass.type = eeq::dsp::peq::Band::Type::lowPass;
    lowPass.frequency = 100.0;
    lowPass.q = 0.7071;

    const auto lp = eeq::dsp::peq::coeffsFor (lowPass, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (lp, 10.0, sampleRate), 0.0, 0.1);
    EXPECT_LT (eeq::dsp::peq::magnitudeDb (lp, 2000.0, sampleRate), -30.0);

    eeq::dsp::peq::Band highPass;
    highPass.type = eeq::dsp::peq::Band::Type::highPass;
    highPass.frequency = 100.0;
    highPass.q = 0.7071;

    const auto hp = eeq::dsp::peq::coeffsFor (highPass, sampleRate);
    EXPECT_NEAR (eeq::dsp::peq::magnitudeDb (hp, 2000.0, sampleRate), 0.0, 0.1);
    EXPECT_LT (eeq::dsp::peq::magnitudeDb (hp, 10.0, sampleRate), -30.0);
}

TEST (PEQ, CascadeSumsBandResponses)
{
    eeq::dsp::peq::Band first;
    first.type = eeq::dsp::peq::Band::Type::bell;
    first.frequency = 1000.0;
    first.gainDb = 4.0;
    first.q = 2.0;

    eeq::dsp::peq::Band second;
    second.type = eeq::dsp::peq::Band::Type::bell;
    second.frequency = 4000.0;
    second.gainDb = -3.0;
    second.q = 2.0;

    eeq::dsp::peq::Cascade cascade;
    cascade.setBands ({ first, second }, sampleRate);

    // 1kHzでは1つ目が効き、2つ目はほぼ0
    EXPECT_NEAR (cascade.magnitudeDbAt (1000.0, sampleRate), 4.0, 0.1);
    EXPECT_NEAR (cascade.magnitudeDbAt (4000.0, sampleRate), -3.0, 0.1);
}

TEST (PEQ, DisabledBandsAreSkipped)
{
    eeq::dsp::peq::Band band;
    band.type = eeq::dsp::peq::Band::Type::bell;
    band.frequency = 1000.0;
    band.gainDb = 6.0;
    band.q = 2.0;
    band.enabled = false;

    eeq::dsp::peq::Cascade cascade;
    cascade.setBands ({ band }, sampleRate);

    EXPECT_NEAR (cascade.magnitudeDbAt (1000.0, sampleRate), 0.0, 1.0e-6);
}
