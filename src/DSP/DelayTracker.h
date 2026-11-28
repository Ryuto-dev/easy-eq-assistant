#pragma once

#include <cmath>
#include <deque>

namespace eeq::dsp
{

// 遅延の自動追従（Delay Tracker、#72）の判定。UI から約 1 秒ごとに推定値を渡すと、何をすべきかを返す。
//  - 推定が無効（信号なし・確からしさ不足）なら何も適用しない（lost）
//  - 適用中の値から threshold 以上動いたら追従する（apply。表示のみのモードでは candidate）
//  - 直近 stableCount 回が互いに threshold 以内なら「安定」。安定で止めるモードでは stopStable を 1 回返す
// 純粋なロジックなので、遅延が変化する系列でテストできる。
class DelayTracker
{
public:
    enum class Mode { applyAndStopWhenStable, applyContinuously, displayOnly };
    enum class Action { none, lost, apply, candidate, stable, stopStable };

    struct Result
    {
        Action action = Action::none;
        double delayMs = 0.0;   // 今回の推定（stopStable では安定した値の平均）
        int stableReadings = 0; // 直近で揃っている回数
    };

    Mode mode = Mode::applyAndStopWhenStable;
    double thresholdMs = 0.05; // ≒ 17 mm。これ未満の動きは推定の揺れとみなす
    int stableCount = 4;       // この回数そろったら安定

    void reset() { history.clear(); }

    Result feed (bool valid, double estimateMs, double appliedMs)
    {
        Result result;

        if (! valid || ! std::isfinite (estimateMs))
        {
            history.clear(); // 信号が途切れたら安定判定もやり直す
            result.action = Action::lost;
            return result;
        }

        history.push_back (estimateMs);
        while ((int) history.size() > stableCount)
            history.pop_front();

        result.delayMs = estimateMs;
        result.stableReadings = countStable();

        const auto moved = std::abs (estimateMs - appliedMs) >= thresholdMs;

        if (result.stableReadings >= stableCount)
        {
            double sum = 0.0;
            for (auto v : history)
                sum += v;
            const auto mean = sum / (double) history.size();

            if (mode == Mode::applyAndStopWhenStable)
            {
                result.action = Action::stopStable;
                result.delayMs = mean;
                return result;
            }

            if (moved && mode == Mode::applyContinuously)
            {
                result.action = Action::apply;
                result.delayMs = mean;
                return result;
            }

            result.action = moved ? Action::candidate : Action::stable;
            result.delayMs = moved ? mean : estimateMs;
            return result;
        }

        if (moved)
            result.action = mode == Mode::displayOnly ? Action::candidate : Action::apply;
        else
            result.action = Action::stable;

        return result;
    }

private:
    /** 最新から遡って、最新の値と threshold 以内で並んでいる回数 */
    int countStable() const
    {
        if (history.empty())
            return 0;
        const auto latest = history.back();
        int count = 0;
        for (auto it = history.rbegin(); it != history.rend(); ++it)
        {
            if (std::abs (*it - latest) >= thresholdMs)
                break;
            ++count;
        }
        return count;
    }

    std::deque<double> history;
};

} // namespace eeq::dsp
