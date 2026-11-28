#pragma once

#include "Core/JuceModules.h"

#include <cmath>
#include <vector>

namespace eeq::core
{

// 本番中の周波数ログ（2026-11）。本番モードの間、1 秒ごとに「その 1 秒の RTA の平均（エネルギー平均）」と
// SPL を CSV に追記する。あとからログページで読み込んで、時間×周波数の色の図と帯域バランスの推移で
// 「ここで急に低音が増えた」などを見返す。
//   time,spl_db,leq_1min_db,<中心周波数 Hz>...
//   18:30:01,92.3,91.8,71.2,73.0,...
// 書き込みは 1 行ずつ追記（落ちても途中まで残る）。ファイル名は <開始日時>_spectrum.csv（spl/ フォルダ）。
struct SpectrumLog
{
    std::vector<double> bandHz;              // 各列の中心周波数
    std::vector<juce::String> clock;         // 行ごとの時刻（HH:MM:SS）
    std::vector<double> seconds;             // 最初の行からの経過秒（日付をまたいでも単調）
    std::vector<float> spl, leq1min;
    std::vector<std::vector<float>> levels;  // [行][帯域] dB

    int rows() const noexcept { return (int) seconds.size(); }
    int bands() const noexcept { return (int) bandHz.size(); }

    static juce::String headerLine (const std::vector<double>& centres)
    {
        juce::String line ("time,spl_db,leq_1min_db");
        for (const auto f : centres)
            line << "," << (f < 100.0 ? juce::String (f, 1) : juce::String (juce::roundToInt (f))); // String(double, 0) は全桁出るので使わない
        return line + "\n";
    }

    static juce::String rowLine (const juce::String& clockText, double splDb, double leq1, const std::vector<float>& bandDb)
    {
        juce::String line (clockText);
        line << "," << juce::String (splDb, 1) << "," << juce::String (leq1, 1);
        for (const auto v : bandDb)
            line << "," << juce::String (std::isfinite (v) ? (double) v : -200.0, 1);
        return line + "\n";
    }

    /** 1 行を追記する（ファイルが無ければヘッダーから書く）。失敗したら false */
    static bool append (const juce::File& file, const std::vector<double>& centres, const juce::String& clockText,
                        double splDb, double leq1, const std::vector<float>& bandDb)
    {
        const auto isNew = ! file.existsAsFile();
        juce::FileOutputStream stream (file);

        if (! stream.openedOk())
            return false;

        stream.setPosition (file.getSize());
        const auto text = (isNew ? headerLine (centres) : juce::String()) + rowLine (clockText, splDb, leq1, bandDb);
        stream.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
        stream.flush();
        return stream.getStatus().wasOk();
    }

    static bool parse (const juce::String& text, SpectrumLog& out, juce::String& error)
    {
        out = SpectrumLog();
        auto lines = juce::StringArray::fromLines (text);
        lines.removeEmptyStrings();

        if (lines.size() < 2)
        {
            error = juce::String (u8"記録がありません");
            return false;
        }

        auto header = juce::StringArray::fromTokens (lines[0].trimCharactersAtStart (juce::String::fromUTF8 ("\xef\xbb\xbf")), ",", "");
        if (header.size() < 4 || header[0].trim() != "time")
        {
            error = juce::String (u8"周波数ログの形式ではありません");
            return false;
        }

        for (int i = 3; i < header.size(); ++i)
            out.bandHz.push_back (header[i].getDoubleValue());

        double dayOffset = 0.0, previous = -1.0, first = -1.0;

        for (int l = 1; l < lines.size(); ++l)
        {
            const auto tokens = juce::StringArray::fromTokens (lines[l], ",", "");
            if (tokens.size() != header.size())
                continue; // 書きかけの最後の行など

            const auto hms = juce::StringArray::fromTokens (tokens[0], ":", "");
            if (hms.size() != 3)
                continue;

            auto t = hms[0].getIntValue() * 3600.0 + hms[1].getIntValue() * 60.0 + hms[2].getIntValue() + dayOffset;
            if (previous >= 0.0 && t < previous - 60.0) // 日付をまたいだ
            {
                dayOffset += 86400.0;
                t += 86400.0;
            }
            previous = t;
            if (first < 0.0)
                first = t;

            out.clock.push_back (tokens[0]);
            out.seconds.push_back (t - first);
            out.spl.push_back ((float) tokens[1].getDoubleValue());
            out.leq1min.push_back ((float) tokens[2].getDoubleValue());

            std::vector<float> row ((size_t) out.bands());
            for (int b = 0; b < out.bands(); ++b)
                row[(size_t) b] = (float) tokens[b + 3].getDoubleValue();
            out.levels.push_back (std::move (row));
        }

        if (out.rows() == 0)
        {
            error = juce::String (u8"記録がありません");
            return false;
        }

        return true;
    }

    /** 帯域 [lowHz, highHz] のエネルギーの合計（dB）。行 r */
    double bandSumDb (int r, double lowHz, double highHz) const
    {
        double energy = 0.0;
        for (int b = 0; b < bands(); ++b)
            if (bandHz[(size_t) b] >= lowHz && bandHz[(size_t) b] <= highHz)
                energy += std::pow (10.0, levels[(size_t) r][(size_t) b] / 10.0);
        return 10.0 * std::log10 (std::max (energy, 1.0e-30));
    }
};

} // namespace eeq::core
