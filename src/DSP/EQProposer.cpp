#include "EQProposer.h"

#include <algorithm>
#include <cmath>

namespace eeq::dsp
{

namespace
{
struct WeightedPoint
{
    double freq = 0.0;
    double error = 0.0; // ターゲット − 予測（= 適用が必要なゲイン）
};

struct BumpShape
{
    double q = 2.0;
    double bandwidthOctaves = 1.0;
};

// 点 idx を中心に、誤差が中心値の半分を横切る周波数を探して半値幅からQを求める（仕様 4.2 (4)）
BumpShape estimateBumpShape (const std::vector<WeightedPoint>& points, int centerIndex)
{
    const auto& center = points[(size_t) centerIndex];
    const auto half = center.error * 0.5;
    const auto n = (int) points.size();

    if (std::abs (half) < 1.0e-6 || std::abs (center.error) < 1.0e-6)
        return { 2.0, 1.0 };

    auto sameSign = [&center] (double error)
    { return error * center.error > 0.0; };

    // 中心誤差と同じ符号で、絶対値が半分を下回る点まで広がる
    int lo = centerIndex;

    while (lo > 0 && sameSign (points[(size_t) (lo - 1)].error)
           && std::abs (points[(size_t) (lo - 1)].error) > std::abs (half))
        --lo;

    int hi = centerIndex;

    while (hi < n - 1 && sameSign (points[(size_t) (hi + 1)].error)
           && std::abs (points[(size_t) (hi + 1)].error) > std::abs (half))
        ++hi;

    const auto fLo = std::max (1.0, points[(size_t) lo].freq);
    const auto fHi = std::max (fLo * 1.0001, points[(size_t) hi].freq);

    const auto bandwidthOctaves = std::log2 (fHi / fLo);
    const auto q = center.freq / std::max (1.0, fHi - fLo);

    return { juce::jlimit (0.1, 20.0, q), bandwidthOctaves };
}

// 帯域が既存帯域と最小間隔より近いか（仕様 4.3）
bool tooClose (const std::vector<peq::Band>& bands, double freq, double minSpacingOctaves)
{
    for (const auto& band : bands)
    {
        const auto ratio = std::max (freq, band.frequency) / std::max (1.0, std::min (freq, band.frequency));

        if (std::log2 (ratio) < minSpacingOctaves)
            return true;
    }

    return false;
}
} // namespace

EQProposalResult proposeEQ (const EQProposalInput& input)
{
    EQProposalResult result;
    const auto n = (int) input.frequency.size();

    if (n == 0 || (int) input.measuredDb.size() != n || (int) input.targetDb.size() != n)
        return result;

    result.predictedDb = input.measuredDb;
    result.residualDb.assign ((size_t) n, 0.0);

    auto updateResidual = [&] ()
    {
        for (int i = 0; i < n; ++i)
            result.residualDb[(size_t) i] = input.targetDb[(size_t) i] - result.predictedDb[(size_t) i];
    };

    updateResidual();

    auto inRange = [&] (double f)
    { return f >= input.minFrequency && f <= input.maxFrequency; };

    auto maxResidualInRange = [&] ()
    {
        // コヒーレンスしきい値未満（weight=0）の帯域は誤差を0として扱う（仕様 4.1）
        double maxAbs = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto weight = input.weight.empty() ? 1.0 : input.weight[(size_t) i];

            if (weight <= 0.0 || ! inRange (input.frequency[(size_t) i]))
                continue;

            maxAbs = std::max (maxAbs, std::abs (result.residualDb[(size_t) i]));
        }

        return maxAbs;
    };

    std::vector<WeightedPoint> points ((size_t) n);
    std::vector<bool> excluded ((size_t) n, false); // 谷ルールでスキップした点を二度と選ばないようにする

    for (int iteration = 0; iteration < input.maxBands; ++iteration)
    {
        if (maxResidualInRange() <= input.residualToleranceDb)
        {
            result.converged = true;
            break;
        }

        // 誤差の絶対値が最大の点を探す（仕様 4.2 (2)）。重み0・範囲外・既存帯域と近すぎる点は候補外
        int best = -1;
        double bestAbs = 0.0;

        for (int i = 0; i < n; ++i)
        {
            if (excluded[(size_t) i])
                continue;

            const auto freq = input.frequency[(size_t) i];

            if (! inRange (freq))
                continue;

            const auto weight = input.weight.empty() ? 1.0 : input.weight[(size_t) i];

            if (weight <= 0.0)
                continue;

            const auto error = result.residualDb[(size_t) i];

            if (std::abs (error) <= bestAbs)
                continue;

            if (tooClose (result.bands, freq, input.minSpacingOctaves))
                continue;

            best = i;
            bestAbs = std::abs (error);
        }

        if (best < 0 || bestAbs < 0.25) // 置ける点がない/意味のある誤差が残っていない
            break;

        for (int i = 0; i < n; ++i)
        {
            points[(size_t) i].freq = input.frequency[(size_t) i];
            points[(size_t) i].error = result.residualDb[(size_t) i];
        }

        const auto shape = estimateBumpShape (points, best);
        auto gain = points[(size_t) best].error;

        // ブーストは係数を掛けてカットを優先する（仕様 4.2 (3)）
        if (gain > 0.0)
        {
            gain *= input.boostScale;

            // 狭い谷（半値幅が 1/4 oct より狭い。グリッドが 1/12 oct なので半値幅の
            // 分解能は限られる点に注意）はキャンセレーションの可能性が高いため
            // ブーストしない（仕様 4.3。設定で有効化できる）。
            // この点とその周辺（±1/4 oct）も候補から外す（ノッチの裾を追いかけない）
            if (! input.allowValleyBoost && shape.bandwidthOctaves < 0.25)
            {
                const auto valleyFreq = points[(size_t) best].freq;

                for (int i = 0; i < n; ++i)
                {
                    const auto ratio = std::max (input.frequency[(size_t) i], valleyFreq)
                                     / std::max (1.0, std::min (input.frequency[(size_t) i], valleyFreq));

                    if (std::log2 (ratio) < 0.25)
                        excluded[(size_t) i] = true;
                }

                continue;
            }
        }

        peq::Band band;
        band.type = peq::Band::Type::bell;
        band.frequency = juce::jlimit (input.minFrequency, input.maxFrequency, points[(size_t) best].freq);
        band.gainDb = juce::jlimit (input.cutLimitDb, input.boostLimitDb, gain);
        band.q = juce::jlimit (input.qMin, input.qMax, shape.q);
        band.enabled = true;

        result.bands.push_back (band);
        ++result.iterations;

        // この帯域を適用した予測カーブを計算して残差を更新する（仕様 4.2 (5)）
        peq::Cascade cascade;
        cascade.setBands (result.bands, input.sampleRate);

        for (int i = 0; i < n; ++i)
            result.predictedDb[(size_t) i] = input.measuredDb[(size_t) i]
                                           + cascade.magnitudeDbAt (input.frequency[(size_t) i], input.sampleRate);

        updateResidual();
    }

    // 最終状態で収束判定
    if (maxResidualInRange() <= input.residualToleranceDb)
        result.converged = true;

    return result;
}

} // namespace eeq::dsp
