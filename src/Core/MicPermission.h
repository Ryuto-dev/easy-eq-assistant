#pragma once

#include <functional>

// macOS のマイク利用許可（TCC）の確認と要求。
// 許可がないと、入力デバイスは「開けるけど無音」になる。
// 注意: このヘッダは JUCE に依存しない（.mm から Apple フレームワークと一緒に使う）。

namespace eeq::mic
{

enum class Status { granted, denied, undetermined };

Status getStatus();

/** ユーザーに許可を求める（未決定のときだけシステムダイアログが出る）。
    結果はメッセージスレッドで呼び出し元に返る */
void request (std::function<void (bool granted)> onResult);

} // namespace eeq::mic
