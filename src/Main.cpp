#include "Core/JuceModules.h"

#include "Core/Log.h"

#include "MainComponent.h"

#if JUCE_WINDOWS
// 予期しない終了の際に、例外コードと発生アドレスをログへ残す（仕様 6.3 のログ収集）。
// 現場で「落ちた」報告だけだと原因が追えないため
static LONG WINAPI eeqCrashHandler (EXCEPTION_POINTERS* info)
{
    if (info != nullptr && info->ExceptionRecord != nullptr)
    {
        const auto code = info->ExceptionRecord->ExceptionCode;
        const auto address = reinterpret_cast<unsigned long long> (info->ExceptionRecord->ExceptionAddress);
        eeq::log::write ("CRASH: code=0x" + juce::String::toHexString ((juce::int64) code)
                             + " address=0x" + juce::String::toHexString ((juce::int64) address));
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

class EasyEQAssistantApplication : public juce::JUCEApplication
{
public:
    EasyEQAssistantApplication() = default;

    const juce::String getApplicationName() override    { return eeq::strings::appName; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
       #if JUCE_WINDOWS
        SetUnhandledExceptionFilter (eeqCrashHandler);
       #endif
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override                            { mainWindow = nullptr; }
    void systemRequestedQuit() override                 { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance()
                                  .getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            // 各タブのレイアウトは 1100x700 を前提に組まれているため、それ未満に
            // 縮めると操作部品が潰れたり画面外に出たりする
            setResizeLimits (1100, 700, 8192, 8192);
            centreWithSize (1100, 700);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (EasyEQAssistantApplication)
