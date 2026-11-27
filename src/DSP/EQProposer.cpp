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

    // 中心誤差と同じ符号で、絶対値が半分を上回る点（内側）の範囲を求める
    auto inside = [&] (int index)
    {
        const auto error = points[(size_t) index].error;
        return sameSign (error) && std::abs (error) > std::abs (half);
    };

    int lo = centerIndex;

    while (lo > 0 && inside (lo - 1))
        --lo;

    int hi = centerIndex;

    while (hi < n - 1 && inside (hi + 1))
        ++hi;

    // 内側の端の点と、その外側の点の間で、誤差が半値を横切る周波数を
    // 対数周波数上の線形補間で求める（グリッド点で打ち切ると帯域幅を
    // 過小評価し、Q を過大に推定する。#117）
    auto crossing = [&] (int innerIndex, int outerIndex)
    {
        const auto& inner = points[(size_t) innerIndex];
        const auto& outer = points[(size_t) outerIndex];
        const auto denom = inner.error - outer.error;
        const auto t = std::abs (denom) > 1.0e-12 ? juce::jlimit (0.0, 1.0, (inner.error - half) / denom) : 0.0;
        const auto logInner = std::log (std::max (1.0, inner.freq));
        const auto logOuter = std::log (std::max (1.0, outer.freq));
        return std::exp (logInner + t * (logOuter - logInner));
    };

    const auto fLo = std::max (1.0, lo > 0 ? crossing (lo, lo - 1) : points[(size_t) lo].freq);
    const auto fHi = std::max (fLo * 1.0001, hi < n - 1 ? crossing (hi, hi + 1) : points[(size_t) hi].freq);

    const auto bandwidthOctaves = std::log2 (fHi / fLo);

    // RBJ のベルでは、dB 半値点 f1, f2 と Q の関係は Q = √(f1·f2) / (f2 − f1)
    const auto q = std::sqrt (fLo * fHi) / std::max (1.0e-6, fHi - fLo);

    return { juce::jlimit (0.1, 20.0, q), bandwidthOctaves };
}

// 補正範囲内（weight>0）の「測定 − ターゲット」の加重中央値（#109）。
// 平均ではなく中央値にして、深いノッチや鋭いピークに引っ張られないようにする
double estimateLevelOffset (const EQProposalInput& input)
{
    const auto n = input.frequency.size();
    std::vector<std::pair<double, double>> values; // (差, 重み)

    for (size_t i = 0; i < n; ++i)
    {
        const auto f = input.frequency[i];
        const auto w = input.weight.size() == n ? input.weight[i] : 1.0;
        const auto diff = input.measuredDb[i] - input.targetDb[i];

        if (! (w > 0.0) || f < input.minFrequency || f > input.maxFrequency || ! std::isfinite (diff))
            continue;

        values.emplace_back (diff, w);
    }

    if (values.empty())
        return 0.0;

    std::sort (values.begin(), values.end());

    double total = 0.0;

    for (const auto& v : values)
        total += v.second;

    double cumulative = 0.0;

    for (size_t i = 0; i < values.size(); ++i)
    {
        cumulative += values[i].second;

        if (cumulative >= total * 0.5)
        {
            // ちょうど半分で切れる場合は隣との平均を取る（偶数個の通常の中央値と一致）
            if (std::abs (cumulative - total * 0.5) < 1.0e-12 && i + 1 < values.size())
                return 0.5 * (values[i].first + values[i + 1].first);

            return values[i].first;
        }
    }

    return values.back().first;
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

    auto inRange = [&] (double f)
    { return f >= input.minFrequency && f <= input.maxFrequency; };

    auto weightAt = [&] (int i)
    { return input.weight.size() == (size_t) n ? input.weight[(size_t) i] : 1.0; };

    // ターゲットの全体レベルを測定に合わせる（#109）
    result.appliedLevelOffsetDb = input.autoLevelAlign ? estimateLevelOffset (input) : input.levelOffsetDb;

    if (! std::isfinite (result.appliedLevelOffsetDb))
        result.appliedLevelOffsetDb = 0.0;

    std::vector<double> targetDb ((size_t) n);

    for (int i = 0; i < n; ++i)
        targetDb[(size_t) i] = input.targetDb[(size_t) i] + result.appliedLevelOffsetDb;

    result.predictedDb = input.measuredDb;
    result.residualDb.assign ((size_t) n, 0.0);

    auto updateResidual = [&] ()
    {
        for (int i = 0; i < n; ++i)
            result.residualDb[(size_t) i] = targetDb[(size_t) i] - result.predictedDb[(size_t) i];
    };

    updateResidual();

    auto maxResidualInRange = [&] ()
    {
        // コヒーレンスしきい値未満（weight=0）の帯域は誤差を0として扱う（仕様 4.1）
        double maxAbs = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto weight = weightAt (i);

            if (weight <= 0.0 || ! inRange (input.frequency[(size_t) i]))
                continue;

            maxAbs = std::max (maxAbs, std::abs (result.residualDb[(size_t) i]));
        }

        return maxAbs;
    };

    std::vector<WeightedPoint> points ((size_t) n);
    std::vector<bool> excluded ((size_t) n, false); // 谷ルールでスキップした点を二度と選ばないようにする

    // 谷のスキップは帯域の試行回数に数えない（#110）。スキップのたびに少なくとも1点が
    // 候補から外れるので、無限ループ防止の上限は点数 + 帯域数で十分
    const auto maxAttempts = n + std::max (0, input.maxBands);

    for (int attempt = 0; attempt < maxAttempts && (int) result.bands.size() < input.maxBands; ++attempt)
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

            const auto weight = weightAt (i);

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
