// Apple のヘッダを JUCE より先に include すること（MacTypes の Point と juce::Point の衝突を避ける）。
#import <AVFAudio/AVFAudio.h>
#import <AVFoundation/AVFoundation.h>

#include <juce_events/juce_events.h>

#include "MicPermission.h"

namespace eeq::mic
{
namespace
{
void deliverOnMessageThread (std::function<void (bool)> onResult, bool granted)
{
    // 結果はメッセージスレッドで返す
    juce::MessageManager::callAsync ([onResult, granted]
    {
        onResult (granted);
    });
}
} // namespace

Status getStatus()
{
    // macOS 14 以降は AVAudioApplication が正規の API（AVCaptureDevice 経由の
    // 音声許可は新しい macOS でダイアログが出ないことがある）
    if (@available (macOS 14.0, *))
    {
        switch ([AVAudioApplication sharedInstance].recordPermission)
        {
            case AVAudioApplicationRecordPermissionGranted:     return Status::granted;
            case AVAudioApplicationRecordPermissionDenied:      return Status::denied;
            case AVAudioApplicationRecordPermissionUndetermined: return Status::undetermined;
            default:                                            return Status::undetermined;
        }
    }
    // ↑ sharedApplication / recordPermission は AVAudioApplication.h で宣言されている

    const auto status = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];

    switch (status)
    {
        case AVAuthorizationStatusAuthorized:     return Status::granted;
        case AVAuthorizationStatusDenied:         return Status::denied;
        case AVAuthorizationStatusRestricted:     return Status::denied;
        case AVAuthorizationStatusNotDetermined:  return Status::undetermined;
        default:                                  return Status::undetermined;
    }
}

void request (std::function<void (bool granted)> onResult)
{
    if (@available (macOS 14.0, *))
    {
        [AVAudioApplication requestRecordPermissionWithCompletionHandler: ^ (BOOL granted)
        {
            deliverOnMessageThread (std::move (onResult), granted != NO);
        }];
        return;
    }

    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                              completionHandler: ^ (BOOL granted)
    {
        deliverOnMessageThread (std::move (onResult), granted != NO);
    }];
}

} // namespace eeq::mic
