#include "Log.h"

namespace eeq::log
{
namespace
{
    juce::CriticalSection mutex;
    std::unique_ptr<juce::FileLogger> logger;
}

void write (const juce::String& line)
{
    const juce::ScopedLock lock (mutex);

    if (logger == nullptr)
        logger.reset (juce::FileLogger::createDefaultAppLogger ("EasyEQAssistant", "eeq.log",
                                                                "Easy EQ assistant log", 1024 * 1024));

    if (logger != nullptr)
        logger->logMessage (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + line);
}

} // namespace eeq::log
