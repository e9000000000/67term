#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "app.h"
#include "cli.h"
#include "crashlog.h"
#include "headless.h"
#include "pty.h"
#include "util.h"

namespace {

void Report(const wchar_t* message) {
    MessageBoxW(nullptr, message, L"67t", MB_OK | MB_ICONERROR);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    std::vector<std::wstring> arguments;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            arguments.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }
    const LaunchOptions options = ParseOptions(arguments);

    if (options.help) {
        MessageBoxW(nullptr, UsageText().c_str(), L"67t", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (options.version) {
        MessageBoxW(nullptr, L"67t 0.1\nNative Windows terminal (ConPTY)", L"67t",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (!options.error.empty()) {
        const std::wstring message = options.error + L"\n\n" + UsageText();
        MessageBoxW(nullptr, message.c_str(), L"67t", MB_OK | MB_ICONERROR);
        return 2;
    }
    if (options.selfTest) {
        return RunSelfTest(options.logPath);
    }
    if (options.bench) {
        return RunBench(options.benchLines, options.logPath);
    }
    if (options.benchStartup) {
        return RunStartupBench(options.logPath);
    }

    if (!PtySession::available()) {
        Report(L"ConPTY is not available on this version of Windows.\n"
               L"67t requires Windows 10 version 1809 or later.");
        return 1;
    }

    EnableDarkModeForApp();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    crashlog::Install();

    App app(instance);
    if (!app.create(options)) {
        Report(L"Cannot create the 67t window.");
        CoUninitialize();
        return 1;
    }

    const int result = app.run();

    CoUninitialize();
    return result;
}
