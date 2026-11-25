// Apple のヘッダを JUCE より先に include すること（MacTypes の Point と juce::Point の衝突を避ける）。
#import <AVFoundation/AVFoundation.h>

#include <juce_events/juce_events.h>

#include "MicPermission.h"

namespace eeq::mic
{

Status getStatus()
{
    const auto status = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];

    switch (status)
    {
        case AVAuthorizationStatusAuthorized:      return Status::granted;
        case AVAuthorizationStatusDenied:          return Status::denied;
        case AVAuthorizationStatusRestricted:      return Status::denied;
        case AVAuthorizationStatusNotDetermined:   return Status::undetermined;
        default:                                   return Status::undetermined;
    }
}

void request (std::function<void (bool granted)> onResult)
{
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                              completionHandler: ^ (BOOL granted)
    {
        // 結果はメッセージスレッドで返す
        juce::MessageManager::callAsync ([onResult, granted]
        {
            onResult (granted != NO);
        });
    }];
}

} // namespace eeq::mic
