#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#endif

// 実機のオーディオデバイスを使う試験。EEQ_HARDWARE_TESTS=1 のときだけ動く（CI では飛ばす）。
//
// 2026-11-28 のクラッシュ: 入力と出力が別デバイス（JUCE の AudioIODeviceCombiner）のまま
// 出力を切り替えると、CoreAudio の通知スレッドが解放済みの結合デバイスの restartAsync() を
// 呼んで落ちた（cmake/patches/juce-coreaudio-listener-race.patch で修正）。
// ここでは入力/出力の組み合わせを、メッセージループを回しながら高速に切り替え続ける。
namespace
{
struct CountingCallback final : juce::AudioIODeviceCallback
{
    std::atomic<int> blocks { 0 };

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outs, int numOuts, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOuts; ++ch)
            if (outs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outs[ch], n); // 無音しか出さない

        blocks.fetch_add (1, std::memory_order_relaxed);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;

    while (juce::Time::getMillisecondCounter() < end)
       #if JUCE_MAC
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); // メッセージスレッドのタイマー/非同期更新を回す
       #else
        juce::Thread::sleep (10);
       #endif
}
} // namespace

TEST (DeviceSwitchHardware, RapidInputOutputSwitchingDoesNotCrash)
{
    if (std::getenv ("EEQ_HARDWARE_TESTS") == nullptr)
        GTEST_SKIP() << "set EEQ_HARDWARE_TESTS=1 to run against real audio devices";

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioDeviceManager manager;
    ASSERT_TRUE (manager.initialise (0, 2, nullptr, true).isEmpty());

    auto* type = manager.getCurrentDeviceTypeObject();
    ASSERT_NE (type, nullptr);
    type->scanForDevices();

    const auto inputs = type->getDeviceNames (true);
    const auto outputs = type->getDeviceNames (false);
    ASSERT_FALSE (outputs.isEmpty());

    CountingCallback callback;
    manager.addAudioCallback (&callback);

    int opened = 0;
    const int rounds = 6;

    for (int round = 0; round < rounds; ++round)
    {
        for (int o = 0; o < outputs.size(); ++o)
        {
            auto setup = manager.getAudioDeviceSetup();
            setup.outputDeviceName = outputs[o];
            setup.inputDeviceName = inputs.isEmpty() ? juce::String() : inputs[(o + round) % inputs.size()];
            setup.useDefaultInputChannels = true;
            setup.useDefaultOutputChannels = true;

            if (manager.setAudioDeviceSetup (setup, true).isEmpty())
                ++opened;

            // 通知が届くタイミングを散らす（すぐ切り替える回と、少し待つ回）
            pumpMessages ((round % 2 == 0) ? 15 : 120);
        }
    }

    pumpMessages (500);
    manager.removeAudioCallback (&callback);
    manager.closeAudioDevice();
    pumpMessages (300);

    EXPECT_GT (opened, 0);
    EXPECT_GT (callback.blocks.load(), 0);
}
