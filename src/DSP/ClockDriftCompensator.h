#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace eeq::dsp
{

// Ref の遅延線とクロックドリフト補正（解析スレッドだけが使う）。
//
// 出力デバイスと入力デバイスのクロックが違うと、内部参照（アプリの出力信号）とマイク入力の
// 時間差が一定の速さで動き続ける。MacBook のスピーカー → BMG-22M では約 90 ppm（4.3 サンプル/秒）で、
// 1 kHz 以上のコヒーレンスがほぼ 0 になっていた（macOS の集約デバイスのドリフト補正でも直らなかった）。
// 同じクロックの組（内蔵マイク＋内蔵スピーカー）では 6 秒間 2970.1 サンプルのまま動かない。
//
// 方式: 生の Ref/Meas の遅延を定期的に観測（addObservation）し、直近の観測へ直線を当てはめて
// ドリフト速度を求める。補正中は Ref を「基準遅延 + bias + ドリフト分」だけ、Meas を bias だけ遅らせる
// （負のドリフトに備えて両方に bias を足す）。小数遅延は窓付き sinc のポリフェーズ補間で、
// サンプルごとに滑らかに変える。|ドリフト| が小さい（同じクロック）ときは何もしない＝従来どおりの
// 整数遅延線だけ（ビット単位で同じ結果）。
class ClockDriftCompensator
{
public:
    static constexpr int halfTaps = 16;           // 補間フィルタの片側タップ数
    // 補正中に Meas/Ref の両方へ足す遅延（サンプル）。Ref の小数補間には先のサンプルが要るので最低 2·halfTaps。
    // 負のドリフト（遅延が縮む）は Ref の遅延を基準より短くする必要があるので、Meas 側を余分に遅らせる
    static constexpr int positiveDriftBias = 2 * halfTaps;
    static constexpr int negativeDriftBias = 8192; // 83 ppm で約 33 分ぶん
    static constexpr double engagePpm = 2.0;      // これ以上ずれていたら補正を始める
    static constexpr double releasePpm = 1.0;     // これ未満になったら補正をやめる（ヒステリシス）
    static constexpr int historySize = 24;        // 直線当てはめに使う観測数（0.5 秒ごとなら 12 秒分）
    static constexpr int minObservations = 6;
    static constexpr double outlierSamples = 3.0;  // 直線からこれ以上外れた観測は捨てる
    static constexpr int maxRejectedInARow = 20;   // 外れ値がこれだけ続いたら測り直す（0.5 秒ごとなら 10 秒）

    explicit ClockDriftCompensator (int lineSize)
        : refLine ((size_t) lineSize, 0.0f), measLine ((size_t) lineSize, 0.0f)
    {
        buildTable();
    }

    /** 直線当てはめに必要な観測期間（サンプル。既定 3 秒 @48 kHz） */
    void setMinimumSpan (double samples) noexcept { minSpan = samples; }

    /** 基準遅延（ユーザーが設定した Ref 遅延）が変わったとき。ドリフト分をここから測り直す */
    void rebase() noexcept { rebasePending = true; baseTime = (double) samplesProcessed; }

    /** 生の Ref/Meas の遅延の観測（観測窓の中心の時刻 = 処理済みサンプル数で）。 */
    void addObservation (double sampleTime, double delaySamples)
    {
        // 当てはめた直線から大きく外れた観測は捨てる（鳴らし始め・止めた直後の窓、反射に引っ張られた推定など）。
        // クロックの差はデバイスの性質で急には変わらないので、直線が決まった後は外れ値で上書きさせない。
        // 外れ値が続く（約 10 秒）ときは経路そのものが変わった（マイクを動かした等）とみなして測り直す
        if (haveFit && ! retracking && std::abs (delaySamples - predict (sampleTime)) > outlierSamples)
        {
            if (++rejectedInARow < maxRejectedInARow)
                return;

            history.clear();
            retracking = true;
        }

        rejectedInARow = 0;
        history.push_back ({ sampleTime, delaySamples });

        while ((int) history.size() > historySize)
            history.pop_front();

        fit();
    }

    /** 1 ブロック分を遅延線に通す。refOut/measOut は numSamples 個 */
    void process (const float* ref, const float* meas, float* refOut, float* measOut, int numSamples,
                  int baseDelay) noexcept
    {
        const auto lineSize = (int) refLine.size();

        for (int i = 0; i < numSamples; ++i)
        {
            refLine[(size_t) writePos] = ref[i];
            measLine[(size_t) writePos] = meas[i];

            if (! active)
            {
                auto readPos = writePos - baseDelay;
                if (readPos < 0)
                    readPos += lineSize;

                refOut[i] = refLine[(size_t) readPos];
                measOut[i] = meas[i];
            }
            else
            {
                // ドリフト分: 傾きで進め、当てはめた直線の予測へゆっくり寄せる（観測ノイズで跳ねない）
                offset += rate;
                const auto target = predict ((double) samplesProcessed) - lockDelay;
                offset += 2.0e-5 * (target - offset);

                const auto totalDelay = (double) (baseDelay + activeBias) + offset;
                refOut[i] = readFractional (refLine, totalDelay);

                auto measRead = writePos - activeBias;
                if (measRead < 0)
                    measRead += lineSize;
                measOut[i] = measLine[(size_t) measRead];
            }

            writePos = (writePos + 1) % lineSize;
            ++samplesProcessed;
        }

        // 補正の開始・停止・振り切れは、時間合わせが変わるので呼び出し側が平均を捨てる
        updateState (baseDelay);
    }

    /** 時間合わせが不連続に変わったか（開始・停止・振り切れ。1度だけ true） */
    bool consumeAlignmentChanged() noexcept
    {
        const auto changed = alignmentChanged;
        alignmentChanged = false;
        if (changed)
            ++alignmentChanges;
        return changed;
    }

    bool isActive() const noexcept { return active; }
    double getDriftPpm() const noexcept { return haveFit ? rate * 1.0e6 : 0.0; }
    std::uint64_t getSamplesProcessed() const noexcept { return samplesProcessed; }
    int getRelockCount() const noexcept { return relocks; }
    double getOffsetForTest() const noexcept { return offset; }
    /** 当てはめた直線: 生の遅延 ≈ getIntercept() + getRate() · t（t = 処理済みサンプル数） */
    double getIntercept() const noexcept { return intercept; }
    double getRate() const noexcept { return rate; }
    double predictForTest (double t) const noexcept { return predict (t); }
    int getAlignmentChangeCount() const noexcept { return alignmentChanges; }

    /** 観測履歴を捨てる（デバイス・サンプルレートが変わったとき） */
    void resetTracking() noexcept
    {
        history.clear();
        retracking = false;
        rejectedInARow = 0;
        haveFit = false;
        rate = 0.0;

        if (active)
        {
            active = false;
            alignmentChanged = true;
        }
    }

private:
    struct Observation
    {
        double time, delay;
    };

    static constexpr int phases = 512;

    void buildTable()
    {
        // table[p][t]: 小数位置 f = p / phases のときの係数（t = 0..2*halfTaps-1 → オフセット t-halfTaps+1）
        table.assign ((size_t) (phases + 1) * (size_t) (2 * halfTaps), 0.0f);
        const auto pi = 3.14159265358979323846;

        for (int p = 0; p <= phases; ++p)
        {
            const auto f = (double) p / (double) phases;
            double sum = 0.0;

            for (int t = 0; t < 2 * halfTaps; ++t)
            {
                const auto x = (double) (t - halfTaps + 1) - f;
                const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                const auto window = 0.5 + 0.5 * std::cos (pi * x / (double) halfTaps); // Hann
                const auto value = sinc * (std::abs (x) < (double) halfTaps ? window : 0.0);
                table[(size_t) p * (size_t) (2 * halfTaps) + (size_t) t] = (float) value;
                sum += value;
            }

            // 直流ゲインを 1 に揃える
            for (int t = 0; t < 2 * halfTaps; ++t)
                table[(size_t) p * (size_t) (2 * halfTaps) + (size_t) t] /= (float) sum;
        }
    }

    // 書き込み位置から totalDelay サンプル前の値（小数）を補間で読む
    float readFractional (const std::vector<float>& line, double totalDelay) const noexcept
    {
        const auto lineSize = (int) line.size();
        const auto whole = std::floor (totalDelay);
        const auto frac = totalDelay - whole;        // 0..1
        const auto k = (int) whole;

        // y = s(n - k - frac) = Σ_t s[n - k - t] · h(t - frac)
        const auto phasePos = frac * (double) phases;
        const auto p0 = (int) phasePos;
        const auto mix = (float) (phasePos - (double) p0);
        const auto* h0 = &table[(size_t) p0 * (size_t) (2 * halfTaps)];
        const auto* h1 = &table[(size_t) (p0 + 1) * (size_t) (2 * halfTaps)];

        float acc = 0.0f;

        for (int t = 0; t < 2 * halfTaps; ++t)
        {
            const auto offsetT = t - halfTaps + 1;
            auto idx = writePos - k - offsetT;
            idx %= lineSize;
            if (idx < 0)
                idx += lineSize;

            const auto h = h0[t] + mix * (h1[t] - h0[t]);
            acc += line[(size_t) idx] * h;
        }

        return acc;
    }

    double predict (double time) const noexcept { return intercept + rate * time; }

    void fit()
    {
        const auto count = (int) history.size();

        if (count < minObservations)
            return;

        // 1) 外れ値に強い直線（Theil–Sen: 2 点ずつの傾きの中央値、切片は残差の中央値）。
        //    鳴らし始めの窓などで外れた観測が 1 つあっても最小二乗のように引きずられない
        const auto t0 = history.front().time;
        std::vector<double> slopes;
        slopes.reserve ((size_t) (count * (count - 1) / 2));

        for (int i = 0; i < count; ++i)
            for (int j = i + 1; j < count; ++j)
                if (const auto dt = history[(size_t) j].time - history[(size_t) i].time; dt > 0.0)
                    slopes.push_back ((history[(size_t) j].delay - history[(size_t) i].delay) / dt);

        if (slopes.empty())
            return;

        auto median = [] (std::vector<double>& v)
        {
            const auto mid = v.size() / 2;
            std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) mid, v.end());
            return v[mid];
        };

        const auto robustSlope = median (slopes);
        std::vector<double> intercepts;
        intercepts.reserve ((size_t) count);

        for (const auto& o : history)
            intercepts.push_back (o.delay - robustSlope * (o.time - t0));

        const auto robustIntercept = median (intercepts);

        // 2) 直線から 1.5 サンプル以内の観測だけで最小二乗をやり直す
        double st = 0, sd = 0, stt = 0, std_ = 0, tMin = 1.0e300, tMax = -1.0e300;
        int used = 0;

        for (const auto& o : history)
        {
            const auto t = o.time - t0;

            if (std::abs (o.delay - (robustIntercept + robustSlope * t)) > 1.5)
                continue;

            st += t; sd += o.delay; stt += t * t; std_ += t * o.delay;
            tMin = std::min (tMin, o.time);
            tMax = std::max (tMax, o.time);
            ++used;
        }

        if (used < minObservations || tMax - tMin < minSpan)
            return;

        const auto n = (double) used;
        const auto denom = n * stt - st * st;

        if (denom <= 0.0)
            return;

        const auto b = (n * std_ - st * sd) / denom;
        const auto aLocal = (sd - b * st) / n;

        double ss = 0;
        for (const auto& o : history)
        {
            const auto t = o.time - t0;

            if (std::abs (o.delay - (robustIntercept + robustSlope * t)) > 1.5)
                continue;

            const auto r = o.delay - (aLocal + b * t);
            ss += r * r;
        }

        if (std::sqrt (ss / n) > 0.75) // ばらつきが大きい（反射・騒音で推定が安定しない）ときは使わない
            return;

        const auto newIntercept = aLocal - b * t0;

        if (retracking && haveFit && active)
        {
            // 測り直した直線へ移るとき、今の時間合わせは保つ（経路の遅延が跳んだ分まで追いかけない）
            const auto now = history.back().time;
            lockDelay += (newIntercept + b * now) - predict (now);
        }

        retracking = false;
        intercept = newIntercept;
        rate = b;
        haveFit = true;
    }

    void updateState (int baseDelay) noexcept
    {
        const auto ppm = std::abs (getDriftPpm());
        const auto now = (double) samplesProcessed;

        const auto biasForRate = rate >= 0.0 ? positiveDriftBias : negativeDriftBias;
        const auto fitsInLine = (double) (baseDelay + biasForRate + negativeDriftBias + 2 * halfTaps) < (double) refLine.size();

        if (! active && haveFit && ppm >= engagePpm && fitsInLine)
        {
            // ゼロ点は「基準遅延を設定した時刻」の実遅延。そこから今までのドリフトも一気に打ち消す
            active = true;
            activeBias = biasForRate;
            lockDelay = predict (baseTime);
            offset = predict (now) - lockDelay;
            rebasePending = false;
            alignmentChanged = true;
        }
        else if (active && (! haveFit || ppm < releasePpm || ! fitsInLine))
        {
            active = false;
            alignmentChanged = true;
        }

        if (active && rebasePending)
        {
            // 基準遅延が変わった: 今の実遅延をゼロ点に取り直す（ユーザーの遅延は今の実測値）
            lockDelay = predict (now);
            offset = 0.0;
            rebasePending = false;
            alignmentChanged = true;
        }

        // Ref の遅延が遅延線に収まらなくなりそうなら、ゼロ点を取り直す（長時間のドリフトの最後の手段。
        // この時点から基準遅延がずれるので、遅延の検出をやり直すのが望ましい）
        const auto total = (double) (baseDelay + activeBias) + offset;

        if (active && (total < (double) halfTaps + 1.0 || total + (double) halfTaps >= (double) refLine.size() - 1.0))
        {
            lockDelay = predict (now);
            offset = 0.0;
            ++relocks;
            alignmentChanged = true;
        }
    }

    std::vector<float> refLine, measLine, table;
    int writePos = 0;
    std::uint64_t samplesProcessed = 0;

    std::deque<Observation> history;
    bool haveFit = false;
    bool retracking = false;
    int rejectedInARow = 0;
    double intercept = 0.0, rate = 0.0; // 生の遅延 ≈ intercept + rate · t（t = 処理済みサンプル数）

    bool active = false;
    bool rebasePending = false;
    bool alignmentChanged = false;
    double lockDelay = 0.0;  // 補正のゼロ点（その時点の実遅延の予測）
    double offset = 0.0;     // ゼロ点からのドリフト分（サンプル）
    double minSpan = 144000.0;
    double baseTime = 0.0;   // 基準遅延を設定した時刻（処理済みサンプル数）
    int activeBias = positiveDriftBias;
    int relocks = 0;
    int alignmentChanges = 0;
};

} // namespace eeq::dsp
