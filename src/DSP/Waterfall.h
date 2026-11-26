#pragma once

#include <cmath>
#include <deque>
#include <vector>

namespace eeq::dsp
{

// 本番モードのウォーターフォール表示用データ（仕様 5.4.1）。
// 横軸=時間、縦軸=周波数（対数）、色=レベル。持続する狭帯域のピーク（ハウリングの
// 始まりなど）が帯として見えるように、一定間隔で 1/N オクターブ帯域のレベルを溜める。
class Waterfall
{
public:
    void configure (int bandsPerOctave, double lengthSeconds, double minDb, double maxDb)
    {
        bandsPerOctave = juce::jlimit (6, 24, bandsPerOctave);
        this->lengthSeconds = juce::jlimit (10.0, 600.0, lengthSeconds);
        this->minDb = minDb;
        this->maxDb = juce::jmax (minDb + 10.0, maxDb);

        if (bandsPerOctave != this->bandsPerOctave)
        {
            this->bandsPerOctave = bandsPerOctave;
            rows.clear();
        }

        updateMaxColumns();
    }

    /** 表示する dB レンジ（色の対応。現在のレベルに合わせて随時更新できる） */
    void setDbRange (double newMinDb, double newMaxDb)
    {
        minDb = newMinDb;
        maxDb = juce::jmax (minDb + 10.0, newMaxDb);
    }

    /** RTAのバンドレベル（dB）を1列ぶん追加する。columnSeconds は列間の時間間隔 */
    void push (const std::vector<float>& bandLevelsDb, double columnSeconds)
    {
        if (bandLevelsDb.empty())
            return;

        // 保持列数は「保持時間 ÷ 実際の列間隔」で決める。列間隔が変わったら
        // 上限を再計算する（間隔を無視すると保持時間が設定の数倍になる）
        if (columnSeconds > 0.0 && std::abs (columnSeconds - this->columnSeconds) > 1.0e-9)
        {
            this->columnSeconds = columnSeconds;
            updateMaxColumns();
        }

        rows.push_back (bandLevelsDb);

        while ((int) rows.size() > maxColumns)
            rows.pop_front();
    }

    /** row=古い順（0=最も古い） */
    const std::vector<float>& row (int index) const { return rows[(size_t) index]; }
    int numRows() const noexcept { return (int) rows.size(); }
    int numBands() const noexcept { return rows.empty() ? 0 : (int) rows.front().size(); }
    double getMinDb() const noexcept { return minDb; }
    double getMaxDb() const noexcept { return maxDb; }

    void clear() { rows.clear(); }

private:
    void updateMaxColumns()
    {
        maxColumns = (int) juce::jlimit (60.0, 2000.0, lengthSeconds / columnSeconds);
    }

    std::deque<std::vector<float>> rows;
    int bandsPerOctave = 6;
    int maxColumns = 600;
    double columnSeconds = 0.1;
    double lengthSeconds = 60.0;
    double minDb = -80.0;
    double maxDb = -20.0;
};

} // namespace eeq::dsp
