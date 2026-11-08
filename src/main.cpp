#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <vector>

#include "app.h"
#include "cli.h"
#include "crashlog.h"
#include "defterm.h"
#include "handoff.h"
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
            const std::wstring argument = argv[i];
            if (_wcsicmp(argument.c_str(), L"-Embedding") == 0 ||
                _wcsicmp(argument.c_str(), L"/Embedding") == 0) {
                continue;
            }
            arguments.push_back(argument);
        }
        LocalFree(argv);
    }
    const bool embedding = handoff::IsEmbeddingLaunch();
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
    if (options.setDefaultTerminal || options.restoreDefaultTerminal) {
        std::wstring error;
        const bool ok = options.setDefaultTerminal ? defterm::SetAsDefault(error)
                                                   : defterm::RestorePrevious(error);
        const std::wstring text =
            ok ? (options.setDefaultTerminal ? L"ok: 67t is the default terminal"
                                             : L"ok: previous default terminal restored")
               : L"error: " + error;
        if (!options.logPath.empty()) {
            std::wstring writeError;
            WriteTextFileAtomic(options.logPath, WideToUtf8(text + L"\r\n"), writeError);
        } else {
            MessageBoxW(nullptr, text.c_str(), L"67t Default Terminal",
                        MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
        }
        return ok ? 0 : 1;
    }

    if (!PtySession::available()) {
        Report(L"ConPTY is not available on this version of Windows.\n"
               L"67t requires Windows 10 version 1809 or later.");
        return 1;
    }

    EnableDarkModeForApp();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    crashlog::Install();

    std::unique_ptr<PtyHandoff> pendingHandoff;
    if (embedding) {
        pendingHandoff = handoff::RunServer(instance);
        if (!pendingHandoff) {
            CoUninitialize();
            return 0;
        }
    }

    App app(instance);
    if (!app.create(options, std::move(pendingHandoff))) {
        Report(L"Cannot create the 67t window.");
        CoUninitialize();
        return 1;
    }

    const int result = app.run();

    CoUninitialize();
    return result;
}
