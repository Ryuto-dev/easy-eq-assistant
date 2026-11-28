#include <gtest/gtest.h>

#include "Core/SpectrumLog.h"

// 本番の周波数ログ（2026-11）: 追記して読み戻せる、日付またぎ・書きかけの行を扱える
TEST (SpectrumLog, AppendThenParseRoundTrips)
{
    const auto file = juce::File::createTempFile ("_spectrum.csv");
    const std::vector<double> centres { 63.0, 1000.0, 8000.0 };

    ASSERT_TRUE (eeq::core::SpectrumLog::append (file, centres, "23:59:58", 90.0, 89.5, { 80.0f, 70.0f, 60.0f }));
    ASSERT_TRUE (eeq::core::SpectrumLog::append (file, centres, "23:59:59", 91.0, 89.7, { 86.0f, 70.0f, 60.0f }));
    ASSERT_TRUE (eeq::core::SpectrumLog::append (file, centres, "00:00:00", 92.0, 90.0, { 80.0f, 70.0f, 60.0f }));
    file.appendText ("00:00:01,93.0"); // 書きかけの行（列が足りない）は読み飛ばす

    eeq::core::SpectrumLog log;
    juce::String error;
    ASSERT_TRUE (eeq::core::SpectrumLog::parse (file.loadFileAsString(), log, error)) << error;
    file.deleteFile();

    ASSERT_EQ (log.bands(), 3);
    ASSERT_EQ (log.rows(), 3);
    EXPECT_DOUBLE_EQ (log.bandHz[1], 1000.0);
    EXPECT_DOUBLE_EQ (log.seconds[0], 0.0);
    EXPECT_DOUBLE_EQ (log.seconds[2], 2.0); // 日付をまたいでも単調
    EXPECT_FLOAT_EQ (log.spl[1], 91.0f);
    EXPECT_FLOAT_EQ (log.levels[1][0], 86.0f);

    // 低域（63 Hz だけ）の合計と全体
    EXPECT_NEAR (log.bandSumDb (1, 30.0, 160.0), 86.0, 1.0e-6);
    EXPECT_NEAR (log.bandSumDb (0, 0.0, 1.0e9), 10.0 * std::log10 (1.0e8 + 1.0e7 + 1.0e6), 1.0e-6);
}

TEST (SpectrumLog, RejectsOtherCsv)
{
    eeq::core::SpectrumLog log;
    juce::String error;
    EXPECT_FALSE (eeq::core::SpectrumLog::parse ("time,spl_db,leq_1min_db,leq_15min_db,leq_session_db,peak_db\n", log, error));
    EXPECT_FALSE (eeq::core::SpectrumLog::parse ("frequency_hz,magnitude_db\n100,1\n", log, error));
}
