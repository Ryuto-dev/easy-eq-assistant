#include "BuiltInSpeakerKeepAlive.h"

#include "../Core/Log.h"

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
 #include <vector>
#endif

namespace eeq::audio
{

#if JUCE_MAC
namespace
{
std::vector<AudioObjectID> allDevices()
{
    AudioObjectPropertyAddress pa { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain };
    UInt32 size = 0;

    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &pa, 0, nullptr, &size) != noErr || size == 0)
        return {};

    std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &pa, 0, nullptr, &size, ids.data()) != noErr)
        return {};

    ids.resize (size / sizeof (AudioObjectID));
    return ids;
}

juce::String deviceName (AudioObjectID id)
{
    AudioObjectPropertyAddress pa { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain };
    CFStringRef name = nullptr;
    UInt32 size = sizeof (name);

    if (AudioObjectGetPropertyData (id, &pa, 0, nullptr, &size, &name) != noErr || name == nullptr)
        return {};

    const auto result = juce::String::fromCFString (name);
    CFRelease (name);
    return result;
}

bool hasStreams (AudioObjectID id, bool input)
{
    AudioObjectPropertyAddress pa { kAudioDevicePropertyStreams,
                                    input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput,
                                    kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    return AudioObjectGetPropertyDataSize (id, &pa, 0, nullptr, &size) == noErr && size > 0;
}

bool isBuiltIn (AudioObjectID id)
{
    AudioObjectPropertyAddress pa { kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain };
    UInt32 transport = 0;
    UInt32 size = sizeof (transport);
    return AudioObjectGetPropertyData (id, &pa, 0, nullptr, &size, &transport) == noErr
        && transport == kAudioDeviceTransportTypeBuiltIn;
}

// 出力のデータソース（'ispk' = 内蔵スピーカー, 'hdpn' = ヘッドフォン）。取れなければ 0
UInt32 outputDataSource (AudioObjectID id)
{
    AudioObjectPropertyAddress pa { kAudioDevicePropertyDataSource, kAudioObjectPropertyScopeOutput,
                                    kAudioObjectPropertyElementMain };
    UInt32 source = 0;
    UInt32 size = sizeof (source);

    if (AudioObjectGetPropertyData (id, &pa, 0, nullptr, &size, &source) != noErr)
        return 0;

    return source;
}

constexpr UInt32 fourCC (const char (&s)[5])
{
    return ((UInt32) (unsigned char) s[0] << 24) | ((UInt32) (unsigned char) s[1] << 16)
         | ((UInt32) (unsigned char) s[2] << 8) | (UInt32) (unsigned char) s[3];
}

// 内蔵スピーカーのデバイス（ヘッドフォンではないもの）
AudioObjectID findBuiltInSpeaker (const std::vector<AudioObjectID>& ids)
{
    AudioObjectID fallback = 0;

    for (auto id : ids)
    {
        if (! isBuiltIn (id) || ! hasStreams (id, false))
            continue;

        const auto source = outputDataSource (id);

        if (source == fourCC ("ispk"))
            return id;

        if (source != fourCC ("hdpn") && fallback == 0)
            fallback = id;
    }

    return fallback;
}

AudioObjectID findByName (const std::vector<AudioObjectID>& ids, const juce::String& name, bool input)
{
    if (name.isEmpty())
        return 0;

    for (auto id : ids)
        if (hasStreams (id, input) && deviceName (id) == name)
            return id;

    return 0;
}

OSStatus silenceProc (AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*,
                      AudioBufferList* output, const AudioTimeStamp*, void*)
{
    if (output != nullptr)
        for (UInt32 i = 0; i < output->mNumberBuffers; ++i)
            if (output->mBuffers[i].mData != nullptr)
                std::memset (output->mBuffers[i].mData, 0, output->mBuffers[i].mDataByteSize);

    return noErr;
}
} // namespace
#endif

BuiltInSpeakerKeepAlive::~BuiltInSpeakerKeepAlive()
{
    stop();
}

bool BuiltInSpeakerKeepAlive::isActive() const noexcept
{
   #if JUCE_MAC
    return procId != nullptr;
   #else
    return false;
   #endif
}

void BuiltInSpeakerKeepAlive::stop()
{
   #if JUCE_MAC
    if (procId != nullptr)
    {
        AudioDeviceStop (runningDevice, (AudioDeviceIOProcID) procId);
        AudioDeviceDestroyIOProcID (runningDevice, (AudioDeviceIOProcID) procId);
        procId = nullptr;
        runningDevice = 0;
        eeq::log::write ("built-in mic workaround: stopped keeping the built-in speakers running");
    }
   #endif
}

bool BuiltInSpeakerKeepAlive::update (const juce::String& inputDeviceName, const juce::String& outputDeviceName)
{
   #if JUCE_MAC
    const auto ids = allDevices();
    const auto inputId = findByName (ids, inputDeviceName, true);
    const auto outputId = findByName (ids, outputDeviceName, false);
    const auto speakerId = findBuiltInSpeaker (ids);

    builtInInput = inputId != 0 && isBuiltIn (inputId);
    const bool needed = builtInInput && speakerId != 0 && outputId != speakerId;

    if (! needed)
    {
        stop();
        return false;
    }

    if (procId != nullptr && runningDevice == speakerId)
        return true;

    stop();

    AudioDeviceIOProcID newProc = nullptr;

    if (AudioDeviceCreateIOProcID (speakerId, silenceProc, nullptr, &newProc) != noErr || newProc == nullptr)
    {
        eeq::log::write ("built-in mic workaround: could not create an IOProc on the built-in speakers");
        return false;
    }

    if (AudioDeviceStart (speakerId, newProc) != noErr)
    {
        AudioDeviceDestroyIOProcID (speakerId, newProc);
        eeq::log::write ("built-in mic workaround: could not start the built-in speakers");
        return false;
    }

    procId = (void*) newProc;
    runningDevice = speakerId;
    eeq::log::write ("built-in mic workaround: keeping \"" + deviceName (speakerId)
                     + "\" running with silence so the built-in mic delivers audio (output is \""
                     + outputDeviceName + "\")");
    return true;
   #else
    juce::ignoreUnused (inputDeviceName, outputDeviceName);
    return false;
   #endif
}

} // namespace eeq::audio
