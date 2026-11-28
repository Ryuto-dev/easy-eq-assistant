#include <gtest/gtest.h>

#include "DSP/DelayTracker.h"

using eeq::dsp::DelayTracker;

// 固定の遅延: 最初に適用し、4 回そろったら安定として止める
TEST (DelayTracker, FixedDelayConvergesAndStops)
{
    DelayTracker tracker;
    double applied = 0.0;
    const double readings[] = { 12.31, 12.32, 12.30, 12.31 };

    auto r = tracker.feed (true, readings[0], applied);
    EXPECT_EQ (r.action, DelayTracker::Action::apply);
    applied = r.delayMs;

    r = tracker.feed (true, readings[1], applied);
    EXPECT_EQ (r.action, DelayTracker::Action::stable);
    r = tracker.feed (true, readings[2], applied);
    EXPECT_EQ (r.action, DelayTracker::Action::stable);
    r = tracker.feed (true, readings[3], applied);
    EXPECT_EQ (r.action, DelayTracker::Action::stopStable);
    EXPECT_NEAR (r.delayMs, 12.31, 0.01);
}

// 信号が無い・確からしさ不足のときは何も適用せず、安定判定もやり直す
TEST (DelayTracker, InvalidEstimatesNeverApply)
{
    DelayTracker tracker;
    EXPECT_EQ (tracker.feed (false, 99.0, 0.0).action, DelayTracker::Action::lost);
    tracker.feed (true, 5.0, 0.0);
    tracker.feed (true, 5.0, 5.0);
    tracker.feed (true, 5.0, 5.0);
    EXPECT_EQ (tracker.feed (false, 0.0, 5.0).action, DelayTracker::Action::lost);
    EXPECT_EQ (tracker.feed (true, 5.0, 5.0).stableReadings, 1); // やり直し
}

// 遅延が動く（気温変化・マイク移動）系列に、続けて追従する
TEST (DelayTracker, FollowsAChangingDelay)
{
    DelayTracker tracker;
    tracker.mode = DelayTracker::Mode::applyContinuously;
    double applied = 0.0;
    int applies = 0;

    for (int i = 0; i < 40; ++i)
    {
        const auto truth = i < 20 ? 10.0 : 10.0 + 0.02 * (i - 20); // 20 回目から 0.02 ms ずつ増える
        const auto r = tracker.feed (true, truth, applied);
        if (r.action == DelayTracker::Action::apply)
        {
            applied = r.delayMs;
            ++applies;
        }
        EXPECT_NE (r.action, DelayTracker::Action::stopStable);
    }

    EXPECT_GE (applies, 3);
    EXPECT_NEAR (applied, 10.38, 0.1); // 最後の真値 10.38 ms に 0.1 ms 以内で付いていく
}

// 表示のみ: 値は出すが適用しない
TEST (DelayTracker, DisplayOnlyNeverApplies)
{
    DelayTracker tracker;
    tracker.mode = DelayTracker::Mode::displayOnly;
    for (int i = 0; i < 10; ++i)
    {
        const auto r = tracker.feed (true, 7.5, 0.0);
        EXPECT_NE (r.action, DelayTracker::Action::apply);
        EXPECT_NE (r.action, DelayTracker::Action::stopStable);
        EXPECT_EQ (r.action, DelayTracker::Action::candidate);
    }
}
