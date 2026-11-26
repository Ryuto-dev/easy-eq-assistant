#pragma once

#include <cstdint>

// ピンクノイズ（−3 dB/oct）。Paul Kellet のフィルタを使用（仕様 3.2 の励起信号）。
// オーディオスレッドで動くので、このクラスはメモリ確保・ロック・ファイルI/Oを一切しない。

namespace eeq::audio
{

class PinkNoiseGenerator
{
public:
    void reset() noexcept
    {
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
    }

    /** 次の1サンプルを返す。実効RMSがほぼ1.0になるよう正規化済み。
        （出力レベル=「dBFS RMS」という統一定義のため。仕様 3.2 / issue #29）
        生の Kellet フィルタ出力のRMSは 0.1933 なので 1/0.1933 を掛けてある。
        ゲイン（フェード・レベル設定）は呼び出し側で掛ける */
    [[nodiscard]] float nextSample() noexcept
    {
        const auto white = whiteNoise() * 0.11f;

        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.76160f * b5 - white * 0.0168980f;

        const auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;

        return pink * unitRmsScale;
    }

private:
    [[nodiscard]] float whiteNoise() noexcept
    {
        // xorshift32。決定的で軽い擬似ホワイトノイズ
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (int32_t) state * (1.0f / 2147483648.0f); // −1..1
    }

    uint32_t state = 0x1234567u; // 0 は不可
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;

    // Kellet フィルタ（white × 0.11）の定常RMSの逆数。tests/AudioTests.cpp が回帰を保証する
    static constexpr float unitRmsScale = 5.1723f;
};

} // namespace eeq::audio
