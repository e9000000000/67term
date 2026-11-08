#include "cli.h"

#include <algorithm>
#include <cwchar>

#include "util.h"

namespace {

bool IsOption(const std::wstring& value, const wchar_t* shortName, const wchar_t* longName) {
    return _wcsicmp(value.c_str(), shortName) == 0 || _wcsicmp(value.c_str(), longName) == 0;
}

}  // namespace

LaunchOptions ParseOptions(const std::vector<std::wstring>& arguments) {
    LaunchOptions options;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::wstring& arg = arguments[i];
        auto next = [&](const wchar_t* name) -> bool {
            if (i + 1 >= arguments.size()) {
                options.error = std::wstring(L"Missing value for ") + name;
                return false;
            }
            ++i;
            return true;
        };

        if (IsOption(arg, L"-s", L"--shell")) {
            if (next(L"--shell")) {
                options.shell = arguments[i];
            }
        } else if (IsOption(arg, L"-d", L"--cwd") || _wcsicmp(arg.c_str(), L"--directory") == 0) {
            if (next(L"--cwd")) {
                options.cwd = arguments[i];
            }
        } else if (_wcsicmp(arg.c_str(), L"--title") == 0) {
            if (next(L"--title")) {
                options.title = arguments[i];
            }
        } else if (_wcsicmp(arg.c_str(), L"--log") == 0) {
            if (next(L"--log")) {
                options.logPath = arguments[i];
            }
        } else if (_wcsicmp(arg.c_str(), L"--selftest") == 0) {
            options.selfTest = true;
        } else if (_wcsicmp(arg.c_str(), L"--bench") == 0) {
            options.bench = true;
            if (i + 1 < arguments.size()) {
                wchar_t* end = nullptr;
                const long value = wcstol(arguments[i + 1].c_str(), &end, 10);
                if (end && *end == 0 && value > 0) {
                    options.benchLines = static_cast<int>(std::min<long>(value, 200000));
                    ++i;
                }
            }
        } else if (_wcsicmp(arg.c_str(), L"--bench-startup") == 0) {
            options.benchStartup = true;
        } else if (_wcsicmp(arg.c_str(), L"--set-default-terminal") == 0) {
            options.setDefaultTerminal = true;
        } else if (_wcsicmp(arg.c_str(), L"--restore-default-terminal") == 0) {
            options.restoreDefaultTerminal = true;
        } else if (_wcsicmp(arg.c_str(), L"--help") == 0 || _wcsicmp(arg.c_str(), L"-h") == 0 ||
                   _wcsicmp(arg.c_str(), L"/?") == 0) {
            options.help = true;
        } else if (_wcsicmp(arg.c_str(), L"--version") == 0 || _wcsicmp(arg.c_str(), L"-v") == 0) {
            options.version = true;
        } else if (!arg.empty() && arg[0] == L'-') {
            options.error = L"Unknown option: " + arg;
        } else if (options.cwd.empty()) {
            if (FileExists(arg)) {
                // A directory starts there; a file starts in its parent.
                const DWORD attributes = GetFileAttributesW(arg.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    options.cwd = FullPath(arg);
                } else {
                    options.cwd = DirectoryOf(FullPath(arg));
                }
            } else {
                options.error = L"Path does not exist: " + arg;
            }
        } else {
            options.error = L"Unexpected argument: " + arg;
        }
    }
    if (!options.cwd.empty()) {
        const DWORD attributes = GetFileAttributesW(options.cwd.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            options.error = L"Not a directory: " + options.cwd;
            options.cwd.clear();
        } else {
            options.cwd = FullPath(options.cwd);
        }
    }
    return options;
}

std::wstring UsageText() {
    return L"67t - a native Windows terminal\n"
           L"\n"
           L"Usage: 67t.exe [options] [directory]\n"
           L"\n"
           L"Options:\n"
           L"  -s, --shell <command>   shell command line (default: pwsh, powershell, cmd)\n"
           L"  -d, --cwd <directory>   start directory\n"
           L"      --title <text>      initial window title\n"
           L"      --selftest          run headless self-tests\n"
           L"      --bench [lines]     run parser/screen microbenchmarks\n"
           L"      --bench-startup     measure hidden chrome construction\n"
           L"      --set-default-terminal   make 67t the Windows default terminal\n"
           L"      --restore-default-terminal  restore the previous default terminal\n"
           L"      --log <path>        write headless output to a file\n"
           L"  -h, --help              show this help\n"
           L"  -v, --version           show the version\n";
}
