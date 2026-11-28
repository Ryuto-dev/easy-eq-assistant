#include "AggregateDevicePair.h"

#include "../Core/Log.h"

#include <vector>

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

namespace eeq::audio
{
namespace
{
// 名前の接頭辞（デバイス一覧から隠すのにも使う）
const juce::String aggregatePrefix { "EasyEQ I/O " };

#if JUCE_MAC
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

juce::String stringProperty (AudioObjectID id, AudioObjectPropertySelector selector)
{
    AudioObjectPropertyAddress pa { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFStringRef value = nullptr;
    UInt32 size = sizeof (value);

    if (AudioObjectGetPropertyData (id, &pa, 0, nullptr, &size, &value) != noErr || value == nullptr)
        return {};

    const auto result = juce::String::fromCFString (value);
    CFRelease (value);
    return result;
}

int channelCount (AudioObjectID id, bool input)
{
    AudioObjectPropertyAddress pa { kAudioDevicePropertyStreamConfiguration,
                                    input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput,
                                    kAudioObjectPropertyElementMain };
    UInt32 size = 0;

    if (AudioObjectGetPropertyDataSize (id, &pa, 0, nullptr, &size) != noErr || size == 0)
        return 0;

    juce::HeapBlock<char> storage (size);
    auto* list = reinterpret_cast<AudioBufferList*> (storage.get());

    if (AudioObjectGetPropertyData (id, &pa, 0, nullptr, &size, list) != noErr)
        return 0;

    int total = 0;

    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
        total += (int) list->mBuffers[i].mNumberChannels;

    return total;
}

AudioObjectID findDevice (const juce::String& name, bool input)
{
    for (auto id : allDevices())
        if (channelCount (id, input) > 0 && stringProperty (id, kAudioObjectPropertyName) == name)
            return id;

    return 0;
}

AudioObjectID findDeviceByUid (const juce::String& uid)
{
    for (auto id : allDevices())
        if (stringProperty (id, kAudioDevicePropertyDeviceUID) == uid)
            return id;

    return 0;
}

// CF の辞書を作る小さなヘルパ（所有権は呼び出し側）
CFMutableDictionaryRef newDictionary()
{
    return CFDictionaryCreateMutable (kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
}

void setString (CFMutableDictionaryRef dict, const char* key, const juce::String& value)
{
    auto cfKey = CFStringCreateWithCString (kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    auto cfValue = value.toCFString();
    CFDictionarySetValue (dict, cfKey, cfValue);
    CFRelease (cfValue);
    CFRelease (cfKey);
}

void setInt (CFMutableDictionaryRef dict, const char* key, int value)
{
    auto cfKey = CFStringCreateWithCString (kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    auto cfValue = CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue (dict, cfKey, cfValue);
    CFRelease (cfValue);
    CFRelease (cfKey);
}
#endif
} // namespace

AggregateDevicePair::~AggregateDevicePair()
{
    destroyAllExcept ({});
}

bool AggregateDevicePair::isAggregateName (const juce::String& name)
{
    return name.startsWith (aggregatePrefix);
}

const AggregateDevicePair::Pair* AggregateDevicePair::find (const juce::String& aggregateName) const
{
    for (auto& c : created)
        if (c.pair.aggregateName == aggregateName)
            return &c.pair;

    return nullptr;
}

const AggregateDevicePair::Pair* AggregateDevicePair::prepare (const juce::String& inputName,
                                                               const juce::String& outputName,
                                                               juce::String& errorOut)
{
    errorOut.clear();

    for (auto& c : created)
        if (c.pair.inputName == inputName && c.pair.outputName == outputName)
            return &c.pair;

   #if JUCE_MAC
    const auto inputId = findDevice (inputName, true);
    const auto outputId = findDevice (outputName, false);

    if (inputId == 0 || outputId == 0 || inputId == outputId)
    {
        errorOut = "device not found";
        return nullptr;
    }

    const auto inputUid = stringProperty (inputId, kAudioDevicePropertyDeviceUID);
    const auto outputUid = stringProperty (outputId, kAudioDevicePropertyDeviceUID);

    if (inputUid.isEmpty() || outputUid.isEmpty())
    {
        errorOut = "device has no UID";
        return nullptr;
    }

    Pair pair;
    pair.inputName = inputName;
    pair.outputName = outputName;
    pair.inputChannels = channelCount (inputId, true);
    pair.outputOffset = channelCount (inputId, false);
    pair.outputChannels = channelCount (outputId, false);
    pair.aggregateName = aggregatePrefix + juce::String (++counter);

    const auto uid = "com.ryuto.easyeqassistant.aggregate." + juce::String (juce::Time::currentTimeMillis())
                   + "." + juce::String (counter);

    // クロックの主は入力デバイス（マイク側）。出力デバイスを OS にドリフト補正させる。
    // 注: MacBook のスピーカー → BMG-22M では OS のドリフト補正を有効にしても約 83 ppm のずれが残った
    // （主を逆にしても同じ）。残るずれは MeasurementEngine の ClockDriftCompensator が補正する。
    // 集約デバイスにする主な効果は、JUCE の結合デバイスの FIFO 破綻（数十サンプルの跳び・音切れ）と
    // デバイス切替時のクラッシュ経路をなくすこと
    const auto clockUid = inputUid;
    auto description = newDictionary();
    setString (description, kAudioAggregateDeviceNameKey, pair.aggregateName);
    setString (description, kAudioAggregateDeviceUIDKey, uid);
    setInt (description, kAudioAggregateDeviceIsPrivateKey, 1);
    setInt (description, kAudioAggregateDeviceIsStackedKey, 0);
    setString (description, kAudioAggregateDeviceMainSubDeviceKey, clockUid); // クロックの主 = 入力デバイス
    setString (description, kAudioAggregateDeviceClockDeviceKey, clockUid);   // クロック源も明示する（JUCE develop と同じ）

    auto subDevices = CFArrayCreateMutable (kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);

    for (const auto& [subUid, drift] : { std::pair<juce::String, int> { inputUid, 0 },
                                         std::pair<juce::String, int> { outputUid, 1 } })
    {
        auto sub = newDictionary();
        setString (sub, kAudioSubDeviceUIDKey, subUid);
        setInt (sub, kAudioSubDeviceDriftCompensationKey, drift);
        setInt (sub, kAudioSubDeviceDriftCompensationQualityKey, 0x7F /* kAudioSubDeviceDriftCompensationMaxQuality */);
        CFArrayAppendValue (subDevices, sub);
        CFRelease (sub);
    }

    {
        auto key = CFStringCreateWithCString (kCFAllocatorDefault, kAudioAggregateDeviceSubDeviceListKey,
                                              kCFStringEncodingUTF8);
        CFDictionarySetValue (description, key, subDevices);
        CFRelease (key);
    }

    AudioObjectID aggregateId = 0;
    const auto status = AudioHardwareCreateAggregateDevice (description, &aggregateId);
    CFRelease (subDevices);
    CFRelease (description);

    if (status != noErr || aggregateId == 0)
    {
        errorOut = "AudioHardwareCreateAggregateDevice failed (" + juce::String ((int) status) + ")";
        eeq::log::write ("aggregate device: create FAILED for \"" + inputName + "\" + \"" + outputName + "\": "
                         + errorOut);
        return nullptr;
    }

    // デバイス一覧に現れるまで待つ（JUCE はこの一覧から名前で探す）
    for (int i = 0; i < 50 && findDeviceByUid (uid) == 0; ++i)
        juce::Thread::sleep (10);

    // ドリフト補正をサブデバイスのオブジェクトに直接設定し、読み返してログに残す。
    // 説明辞書の "drift" キーだけでは効かないことがある（実測でドリフトが残った）ため。
    // サブデバイスは集約デバイスが所有する kAudioSubDeviceClassID のオブジェクト（元デバイスとは別 ID）
    {
        juce::String state;
        AudioObjectPropertyAddress pa { kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain };
        AudioClassID subClass = kAudioSubDeviceClassID;
        UInt32 size = 0;

        if (AudioObjectGetPropertyDataSize (aggregateId, &pa, sizeof (subClass), &subClass, &size) == noErr && size > 0)
        {
            std::vector<AudioObjectID> subs (size / sizeof (AudioObjectID));

            if (AudioObjectGetPropertyData (aggregateId, &pa, sizeof (subClass), &subClass, &size, subs.data()) == noErr)
            {
                for (auto sub : subs)
                {
                    const auto subUid = stringProperty (sub, kAudioDevicePropertyDeviceUID);
                    AudioObjectPropertyAddress dp { kAudioSubDevicePropertyDriftCompensation,
                                                    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
                    UInt32 want = subUid == clockUid ? 0u : 1u;
                    const auto setStatus = AudioObjectSetPropertyData (sub, &dp, 0, nullptr, sizeof (want), &want);

                    AudioObjectPropertyAddress qp { kAudioSubDevicePropertyDriftCompensationQuality,
                                                    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
                    UInt32 quality = 0x7F;
                    if (want != 0)
                        AudioObjectSetPropertyData (sub, &qp, 0, nullptr, sizeof (quality), &quality);

                    UInt32 drift = 0, dsize = sizeof (drift);
                    AudioObjectGetPropertyData (sub, &dp, 0, nullptr, &dsize, &drift);
                    state << " [" << stringProperty (sub, kAudioObjectPropertyName) << " drift=" << (int) drift
                          << (setStatus != noErr ? " (set failed " + juce::String ((int) setStatus) + ")" : juce::String()) << "]";
                }
            }
        }

        eeq::log::write ("aggregate device: sub-devices" + state);
    }

    eeq::log::write ("aggregate device: created \"" + pair.aggregateName + "\" = input \"" + inputName
                     + "\" (clock) + output \"" + outputName + "\" (drift compensated); in=" + juce::String (pair.inputChannels)
                     + "ch out=" + juce::String (pair.outputChannels) + "ch at offset " + juce::String (pair.outputOffset));

    created.push_back ({ pair, (unsigned int) aggregateId });
    return &created.back().pair;
   #else
    juce::ignoreUnused (inputName, outputName);
    errorOut = "not supported";
    return nullptr;
   #endif
}

void AggregateDevicePair::destroyAllExcept (const juce::String& keepAggregateName)
{
    for (auto it = created.begin(); it != created.end();)
    {
        if (it->pair.aggregateName == keepAggregateName)
        {
            ++it;
            continue;
        }

       #if JUCE_MAC
        AudioHardwareDestroyAggregateDevice ((AudioObjectID) it->objectId);
       #endif
        eeq::log::write ("aggregate device: destroyed \"" + it->pair.aggregateName + "\"");
        it = created.erase (it);
    }
}

} // namespace eeq::audio
