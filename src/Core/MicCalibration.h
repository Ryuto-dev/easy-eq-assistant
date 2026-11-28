#pragma once

#include "../DSP/TransferFunctionAccumulator.h"

#include "Core/JuceModules.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace eeq::core
{

// 測定マイクの周波数特性の補正（ロードマップ B-5）。
// miniDSP（UMIK / ECM の .txt。1 行目に "Sens Factor =…dB"）、Dayton / REW / .frd（周波数 dB [位相]）の
// テキストを読み、Meas 側の特性を差し引く: H_corrected(f) = H(f) / M(f)。
// 位相の列があれば位相も戻す（無ければ振幅だけ）。範囲外の周波数は端の値で一定とみなす。
// 不変オブジェクトとして shared_ptr<const> で受け渡す（解析スレッドから読んでも安全）。
class MicCalibration
{
public:
    juce::String name;          // 表示名（ファイル名）
    std::vector<double> freqHz; // 昇順
    std::vector<double> gainDb;
    std::vector<double> phaseDeg; // 空なら位相なし
    double sensitivityDb = 0.0;   // miniDSP の "Sens Factor"（SPL 校正の参考。補正には使わない）
    bool hasSensitivity = false;

    /** テキストを読む。失敗したら nullptr と errorOut */
    static std::shared_ptr<const MicCalibration> parse (const juce::String& text, const juce::String& displayName,
                                                        juce::String& errorOut)
    {
        auto cal = std::make_shared<MicCalibration>();
        cal->name = displayName;

        struct Row { double f, db, ph; bool hasPhase; };
        std::vector<Row> rows;

        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();

            if (line.isEmpty())
                continue;

            // miniDSP: "Sens Factor =-1.23dB, SERNO: 7012345"（"Sensitivity" と書く版もある）
            if (line.containsIgnoreCase ("sens"))
            {
                const auto afterEquals = line.fromFirstOccurrenceOf ("=", false, false).trim();
                if (afterEquals.isNotEmpty())
                {
                    cal->sensitivityDb = afterEquals.getDoubleValue();
                    cal->hasSensitivity = true;
                }
                continue;
            }

            // コメント・見出し（数字で始まらない行）は読み飛ばす
            const auto first = line[0];
            if (! (juce::CharacterFunctions::isDigit (first) || first == '.' || first == '-' || first == '+'))
                continue;

            auto tokens = juce::StringArray::fromTokens (line.replaceCharacters (",;\t", "   "), " ", "\"");
            tokens.removeEmptyStrings();

            if (tokens.size() < 2)
                continue;

            const auto f = tokens[0].getDoubleValue();
            const auto db = tokens[1].getDoubleValue();

            if (! (f > 0.0) || ! std::isfinite (f) || ! std::isfinite (db) || std::abs (db) > 60.0)
                continue;

            Row row { f, db, 0.0, false };
            if (tokens.size() >= 3)
            {
                row.ph = tokens[2].getDoubleValue();
                row.hasPhase = std::isfinite (row.ph);
            }
            rows.push_back (row);
        }

        if (rows.size() < 2)
        {
            errorOut = "no frequency / dB rows";
            return nullptr;
        }

        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.f < b.f; });

        bool allPhase = true;
        for (const auto& r : rows)
            allPhase = allPhase && r.hasPhase;

        for (const auto& r : rows)
        {
            if (! cal->freqHz.empty() && r.f <= cal->freqHz.back())
                continue; // 同じ周波数の重複は最初の行を使う

            cal->freqHz.push_back (r.f);
            cal->gainDb.push_back (r.db);
            if (allPhase)
                cal->phaseDeg.push_back (r.ph);
        }

        if (cal->freqHz.size() < 2)
        {
            errorOut = "not enough distinct frequencies";
            return nullptr;
        }

        return cal;
    }

    /** f における補正値（dB, 度）。対数周波数で線形補間、範囲外は端の値 */
    void valueAt (double f, double& db, double& phase) const noexcept
    {
        phase = 0.0;

        if (freqHz.empty() || ! (f > 0.0))
        {
            db = 0.0;
            return;
        }

        if (f <= freqHz.front())
        {
            db = gainDb.front();
            phase = phaseDeg.empty() ? 0.0 : phaseDeg.front();
            return;
        }

        if (f >= freqHz.back())
        {
            db = gainDb.back();
            phase = phaseDeg.empty() ? 0.0 : phaseDeg.back();
            return;
        }

        const auto upper = std::upper_bound (freqHz.begin(), freqHz.end(), f);
        const auto i = (size_t) std::distance (freqHz.begin(), upper);
        const auto f0 = freqHz[i - 1], f1 = freqHz[i];
        const auto t = (std::log (f) - std::log (f0)) / (std::log (f1) - std::log (f0));
        db = gainDb[i - 1] + t * (gainDb[i] - gainDb[i - 1]);
        if (! phaseDeg.empty())
            phase = phaseDeg[i - 1] + t * (phaseDeg[i] - phaseDeg[i - 1]);
    }

    double dbAt (double f) const noexcept
    {
        double db = 0.0, phase = 0.0;
        valueAt (f, db, phase);
        return db;
    }

    /** フレームの Meas 側からマイクの特性を差し引く（H と Meas のパワー。Ref とコヒーレンスはそのまま） */
    void applyTo (eeq::dsp::SpectrumFrame& frame) const
    {
        if (frame.fftSize <= 0 || frame.sampleRate <= 0.0)
            return;

        const auto binHz = frame.sampleRate / (double) frame.fftSize;
        const auto bins = frame.transfer.size();

        for (size_t k = 1; k < bins; ++k)
        {
            double db = 0.0, phase = 0.0;
            valueAt ((double) k * binHz, db, phase);

            const auto inverse = std::polar (std::pow (10.0, -db / 20.0), -phase * juce::MathConstants<double>::pi / 180.0);
            const auto h = std::complex<double> (frame.transfer[k].real(), frame.transfer[k].imag()) * inverse;
            frame.transfer[k] = { (float) h.real(), (float) h.imag() };

            if (k < frame.magnitudeDb.size())
                frame.magnitudeDb[k] -= (float) db;
            if (k < frame.phaseDeg.size())
                frame.phaseDeg[k] = (float) (std::arg (h) * 180.0 / juce::MathConstants<double>::pi);
            if (k < frame.measPowerDb.size())
                frame.measPowerDb[k] -= (float) db;
        }
    }
};

} // namespace eeq::core
