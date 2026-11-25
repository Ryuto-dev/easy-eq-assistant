#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>
#include <vector>

namespace eeq::dsp::peq
{

// パラメトリックEQの1帯域（仕様 4.3）。
struct Band
{
    enum class Type { bell, lowShelf, highShelf, lowPass, highPass };

    Type type = Type::bell;
    double frequency = 1000.0; // Hz
    double gainDb = 0.0;       // シェルフ/ベルで使用。パスフィルタでは無視
    double q = 0.7071;
    bool enabled = true;
};

// RBJ Audio EQ Cookbook（仕様 4.4）のバイクアッド係数（a0 正規化済み）。
struct Coeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

[[nodiscard]] inline Coeffs coeffsFor (const Band& band, double sampleRate)
{
    Coeffs c;

    const auto f0 = juce::jlimit (1.0, sampleRate * 0.499, band.frequency);
    const auto w0 = 2.0 * juce::MathConstants<double>::pi * f0 / sampleRate;
    const auto cw = std::cos (w0);
    const auto sw = std::sin (w0);
    const auto q = juce::jlimit (0.05, 50.0, band.q);
    const auto alpha = sw / (2.0 * q);
    const auto A = std::pow (10.0, band.gainDb / 40.0); // ベル/シェルフ用

    double a0 = 1.0;

    switch (band.type)
    {
        case Band::Type::bell:
        {
            c.b0 = 1.0 + alpha * A;
            c.b1 = -2.0 * cw;
            c.b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            c.a1 = -2.0 * cw;
            c.a2 = 1.0 - alpha / A;
            break;
        }

        case Band::Type::lowShelf:
        {
            const auto sq = 2.0 * std::sqrt (A) * alpha;
            c.b0 = A * ((A + 1.0) - (A - 1.0) * cw + sq);
            c.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
            c.b2 = A * ((A + 1.0) - (A - 1.0) * cw - sq);
            a0 = (A + 1.0) + (A - 1.0) * cw + sq;
            c.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
            c.a2 = (A + 1.0) + (A - 1.0) * cw - sq;
            break;
        }

        case Band::Type::highShelf:
        {
            const auto sq = 2.0 * std::sqrt (A) * alpha;
            c.b0 = A * ((A + 1.0) + (A - 1.0) * cw + sq);
            c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
            c.b2 = A * ((A + 1.0) + (A - 1.0) * cw - sq);
            a0 = (A + 1.0) - (A - 1.0) * cw + sq;
            c.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
            c.a2 = (A + 1.0) - (A - 1.0) * cw - sq;
            break;
        }

        case Band::Type::lowPass:
        {
            c.b0 = (1.0 - cw) / 2.0;
            c.b1 = 1.0 - cw;
            c.b2 = (1.0 - cw) / 2.0;
            a0 = 1.0 + alpha;
            c.a1 = -2.0 * cw;
            c.a2 = 1.0 - alpha;
            break;
        }

        case Band::Type::highPass:
        {
            c.b0 = (1.0 + cw) / 2.0;
            c.b1 = -(1.0 + cw);
            c.b2 = (1.0 + cw) / 2.0;
            a0 = 1.0 + alpha;
            c.a1 = -2.0 * cw;
            c.a2 = 1.0 - alpha;
            break;
        }
    }

    // a0 正規化
    const auto inv = 1.0 / a0;
    c.b0 *= inv;
    c.b1 *= inv;
    c.b2 *= inv;
    c.a1 *= inv;
    c.a2 *= inv;

    return c;
}

/** バイクアッドの解析的な周波数応答 [dB] */
[[nodiscard]] inline double magnitudeDb (const Coeffs& c, double frequency, double sampleRate)
{
    const auto w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (0.1, sampleRate * 0.499, frequency) / sampleRate;
    const auto z1 = std::polar (1.0, -w);
    const auto z2 = std::polar (1.0, -2.0 * w);

    const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;

    return 20.0 * std::log10 (juce::jmax (std::abs (num) / juce::jmax (std::abs (den), 1.0e-12), 1.0e-12));
}

/** 複数帯域の直列（カスケード）応答。EQ案の予測カーブ計算に使う（仕様 4.4） */
class Cascade
{
public:
    void setBands (const std::vector<Band>& bands, double sampleRate)
    {
        coeffs.clear();

        for (const auto& band : bands)
            if (band.enabled)
                coeffs.push_back (coeffsFor (band, sampleRate));
    }

    [[nodiscard]] double magnitudeDbAt (double frequency, double sampleRate) const
    {
        double total = 0.0;

        for (const auto& c : coeffs)
            total += magnitudeDb (c, frequency, sampleRate);

        return total;
    }

    const std::vector<Coeffs>& getCoeffs() const noexcept { return coeffs; }

private:
    std::vector<Coeffs> coeffs;
};

} // namespace eeq::dsp::peq
