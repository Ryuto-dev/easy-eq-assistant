// macOS 以外向けのマイク許可スタブ。
// Windows には macOS の TCC のようなアプリごとのマイク承認ダイアログがないため、
// 常に「許可済み」として振る舞う（実際のアクセス制御は OS 側の設定に任せる）。

#include "MicPermission.h"

#include <juce_events/juce_events.h>

namespace eeq::mic
{

Status getStatus()
{
    return Status::granted;
}

void request (std::function<void (bool granted)> onResult)
{
    // 結果はメッセージスレッドで返す（macOS 実装と同じ契約）
    juce::MessageManager::callAsync ([onResult]
    {
        onResult (true);
    });
}

} // namespace eeq::mic
