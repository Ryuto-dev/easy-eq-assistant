#include <gtest/gtest.h>

#include "DSP/EQProposer.h"

#include <cmath>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

// 対数グリッド（1/12 oct）を作る
std::vector<double> makeGrid (double fMin, double fMax)
{
    std::vector<double> grid;
    const auto octaves = std::log2 (fMax / fMin);
    const auto steps = (int) std::floor (octaves * 12.0);

    for (int i = 0; i <= steps; ++i)
        grid.push_back (fMin * std::pow (2.0, (double) i / 12.0));

    return grid;
}

eeq::dsp::EQProposalInput makeInput (const std::vector<double>& grid, const std::vector<double>& measured)
{
    eeq::dsp::EQProposalInput input;
    input.frequency = grid;
    input.measuredDb = measured;
    input.targetDb.assign (grid.size(), 0.0);
    input.weight.assign (grid.size(), 1.0);
    input.sampleRate = sampleRate;
    input.minFrequency = 40.0;
    input.maxFrequency = 16000.0;
    return input;
}
} // namespace

TEST (EQProposer, RecoversKnownPeqWithinTolerance)
{
    // 既知のPEQで作った歪み（仕様 7.2 の「意図的な変化」）
    eeq::dsp::peq::Band bump;
    bump.type = eeq::dsp::peq::Band::Type::bell;
    bump.frequency = 1000.0;
    bump.gainDb = 6.0;
    bump.q = 4.0;

    eeq::dsp::peq::Band dip;
    dip.type = eeq::dsp::peq::Band::Type::bell;
    dip.frequency = 250.0;
    dip.gainDb = -5.0;
    dip.q = 2.0;

    eeq::dsp::peq::Cascade room;
    room.setBands ({ bump, dip }, sampleRate);

    const auto grid = makeGrid (40.0, 16000.0);
    std::vector<double> measured;

    for (const auto f : grid)
        measured.push_back (room.magnitudeDbAt (f, sampleRate));

    auto input = makeInput (grid, measured);
    const auto result = eeq::dsp::proposeEQ (input);

    EXPECT_LE ((int) result.bands.size(), input.maxBands);

    // 提案を適用すると、補正範囲の残差が ±1.5 dB 以内に収まる（仕様 4.2 (6)）
    double maxResidual = 0.0;

    for (size_t i = 0; i < grid.size(); ++i)
    {
        if (grid[i] < input.minFrequency || grid[i] > input.maxFrequency)
            continue;

        maxResidual = std::max (maxResidual, std::abs (result.residualDb[i]));
    }

    EXPECT_LE (maxResidual, 1.5);
}

TEST (EQProposer, RespectsGainLimits)
{
    // 大きすぎる山はブースト上限で打ち止めになる
    eeq::dsp::peq::Band huge;
    huge.type = eeq::dsp::peq::Band::Type::bell;
    huge.frequency = 1000.0;
    huge.gainDb = 20.0;
    huge.q = 1.0;

    eeq::dsp::peq::Cascade room;
    room.setBands ({ huge }, sampleRate);

    const auto grid = makeGrid (40.0, 16000.0);
    std::vector<double> measured;

    for (const auto f : grid)
        measured.push_back (room.magnitudeDbAt (f, sampleRate));

    auto input = makeInput (grid, measured);
    const auto result = eeq::dsp::proposeEQ (input);

    ASSERT_FALSE (result.bands.empty());

    for (const auto& band : result.bands)
    {
        EXPECT_LE (band.gainDb, input.boostLimitDb + 1.0e-6);
        EXPECT_GE (band.gainDb, input.cutLimitDb - 1.0e-6);
    }
}

TEST (EQProposer, SkipsNarrowCancellationValleysByDefault)
{
    // 狭く深い谷（1/6 oct より狭い半値幅）はキャンセレーションなので既定ではブーストしない
    eeq::dsp::peq::Band narrow;
    narrow.type = eeq::dsp::peq::Band::Type::bell;
    narrow.frequency = 1000.0;
    narrow.gainDb = -12.0;
    narrow.q = 12.0; // 半値幅が 1/6 oct より十分狭い

    eeq::dsp::peq::Cascade room;
    room.setBands ({ narrow }, sampleRate);

    const auto grid = makeGrid (40.0, 16000.0);
    std::vector<double> measured;

    for (const auto f : grid)
        measured.push_back (room.magnitudeDbAt (f, sampleRate));

    auto input = makeInput (grid, measured);
    input.allowValleyBoost = false;
    const auto withoutBoost = eeq::dsp::proposeEQ (input);

    bool boostedValley = false;

    for (const auto& band : withoutBoost.bands)
        if (band.gainDb > 0.0 && std::abs (band.frequency - 1000.0) < 100.0)
            boostedValley = true;

    EXPECT_FALSE (boostedValley);

    // 設定で有効化すると提案される（仕様 4.3）
    input.allowValleyBoost = true;
    const auto withBoost = eeq::dsp::proposeEQ (input);

    bool boosted = false;

    for (const auto& band : withBoost.bands)
        if (std::abs (band.frequency - 1000.0) < 100.0)
            boosted = true;

    EXPECT_TRUE (boosted);
}

TEST (EQProposer, WeightZeroExcludesRegion)
{
    // 1kHz 帯域のコヒーレンスが低くて weight=0 なら、そこには帯域を置かない
    const auto grid = makeGrid (40.0, 16000.0);
    std::vector<double> measured (grid.size(), 0.0);

    for (size_t i = 0; i < grid.size(); ++i)
    {
        const auto distance = std::abs (grid[i] - 1000.0);

        if (distance < 100.0)
            measured[i] = 6.0 * (1.0 - distance / 100.0);
    }

    auto input = makeInput (grid, measured);

    for (size_t i = 0; i < grid.size(); ++i)
        input.weight[i] = std::abs (grid[i] - 1000.0) < 150.0 ? 0.0 : 1.0;

    const auto result = eeq::dsp::proposeEQ (input);

    for (const auto& band : result.bands)
        EXPECT_FALSE (std::abs (band.frequency - 1000.0) < 150.0);
}

TEST (EQProposer, FlatInputNeedsNoBands)
{
    const auto grid = makeGrid (40.0, 16000.0);
    auto input = makeInput (grid, std::vector<double> (grid.size(), 0.0));

    const auto result = eeq::dsp::proposeEQ (input);

    EXPECT_TRUE (result.bands.empty());
    EXPECT_TRUE (result.converged);
}
