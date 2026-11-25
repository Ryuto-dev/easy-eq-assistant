#pragma once

// 本番モード中の画面スリープ防止（仕様 5.4）。
// スコープを抜けると自動で解除される RAII。

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <IOKit/pwr_mgt/IOPMLib.h>
#endif

namespace eeq::core
{

class KeepAwake
{
public:
    KeepAwake() { enable(); }
    ~KeepAwake() { disable(); }

    void enable()
    {
        if (enabled)
            return;

       #if JUCE_WINDOWS
        previousState = SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        enabled = true;
       #elif JUCE_MAC
        if (IOPMAssertionCreateWithName (kIOPMAssertionTypeNoDisplaySleep,
                                         kIOPMAssertionLevelOn,
                                         CFSTR ("Easy EQ assistant live mode"),
                                         &assertion) == kIOReturnSuccess)
            enabled = true;
       #endif
    }

    void disable()
    {
        if (! enabled)
            return;

       #if JUCE_WINDOWS
        SetThreadExecutionState (previousState);
        enabled = false;
       #elif JUCE_MAC
        IOPMAssertionRelease (assertion);
        assertion = 0;
        enabled = false;
       #endif
    }

private:
    bool enabled = false;

   #if JUCE_WINDOWS
    EXECUTION_STATE previousState = ES_CONTINUOUS;
   #elif JUCE_MAC
    IOPMAssertionID assertion = 0;
   #endif
};

} // namespace eeq::core
