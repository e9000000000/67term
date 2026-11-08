#include "util.h"

#include <dwmapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <cctype>

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring GetLastErrorText(DWORD error) {
    if (error == 0) {
        return L"Unknown error";
    }
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result;
    if (length && buffer) {
        result.assign(buffer, length);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
            result.pop_back();
        }
    } else {
        result = L"Error " + std::to_wstring(error);
    }
    if (buffer) {
        LocalFree(buffer);
    }
    return result;
}

std::wstring GetErrorText(const std::wstring& context, DWORD error) {
    return context + L": " + GetLastErrorText(error);
}

std::wstring ExeDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return L".";
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return DirectoryOf(buffer);
}

std::wstring DataDirectory() {
    const std::wstring exeDir = ExeDirectory();
    if (FileExists(exeDir + L"\\67t.ini")) {
        return exeDir + L"\\state";
    }
    PWSTR appData = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) && appData) {
        result = std::wstring(appData) + L"\\67t";
    } else {
        result = exeDir + L"\\state";
    }
    if (appData) {
        CoTaskMemFree(appData);
    }
    return result;
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

std::wstring FullPath(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return path;
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
    if (written == 0 || written >= needed) {
        return path;
    }
    buffer.resize(written);
    return buffer;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

namespace {

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct ProcessBasicInformation {
    PVOID reserved1;
    PVOID pebBaseAddress;
    PVOID reserved2[2];
    ULONG_PTR uniqueProcessId;
    PVOID reserved3;
};

// Reads RTL_USER_PROCESS_PARAMETERS.CurrentDirectory through the target PEB.
// x64 and WOW64 targets use different structure offsets.
std::wstring ProcessCurrentDirectoryImpl(HANDLE process, NtQueryInformationProcessFn query) {
    ProcessBasicInformation info{};
    ULONG returned = 0;
    if (query(process, 0 /*ProcessBasicInformation*/, &info, sizeof(info), &returned) < 0 ||
        !info.pebBaseAddress) {
        return {};
    }

    BOOL wow64 = FALSE;
    PVOID peb = info.pebBaseAddress;
    if (IsWow64Process(process, &wow64) && wow64) {
        ULONG_PTR peb32 = 0;
        if (query(process, 26 /*ProcessWow64Information*/, &peb32, sizeof(peb32), &returned) < 0 ||
            !peb32) {
            return {};
        }
        peb = reinterpret_cast<PVOID>(peb32);
    }

    const size_t parametersOffset = wow64 ? 0x10 : 0x20;
    const size_t currentDirectoryOffset = wow64 ? 0x24 : 0x38;
    BYTE* parameters = nullptr;
    if (!ReadProcessMemory(process, static_cast<BYTE*>(peb) + parametersOffset, &parameters,
                           sizeof(parameters), nullptr) ||
        !parameters) {
        return {};
    }

    struct CurrentDirectory {
        USHORT length;
        USHORT maximumLength;
        PVOID buffer;
    } directory{};
    if (!ReadProcessMemory(process, parameters + currentDirectoryOffset, &directory,
                           sizeof(directory), nullptr) ||
        !directory.buffer || directory.length == 0 || directory.length >= 65534) {
        return {};
    }

    std::wstring path(directory.length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(process, directory.buffer, path.data(), directory.length, nullptr)) {
        return {};
    }
    while (!path.empty() && path.back() == L'\0') {
        path.pop_back();
    }
    // PEB current directories keep a trailing separator; drop it so paths
    // compare naturally with configured directories (keep drive roots).
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

}  // namespace

std::wstring ProcessCurrentDirectory(DWORD processId) {
    if (processId == 0) {
        return {};
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!process) {
        return {};
    }
    std::wstring result;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        ntdll ? GetProcAddress(ntdll, "NtQueryInformationProcess") : nullptr);
    if (query) {
        result = ProcessCurrentDirectoryImpl(process, query);
    }
    CloseHandle(process);
    return result;
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    if (pos == 2 && path.size() >= 3 && path[1] == L':') {
        return path.substr(0, 3);
    }
    return path.substr(0, pos);
}

std::wstring CompactPathLabel(const std::wstring& path) {
    std::wstring label = FileNameOf(path);
    const std::wstring directory = DirectoryOf(path);
    if (directory != L"." && directory.size() > 3) {
        const std::wstring parent = FileNameOf(directory);
        if (!parent.empty()) {
            label += L"  \u2014  ";
            label += parent;
        }
    }
    return label;
}

bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FormatBytes(unsigned long long bytes) {
    wchar_t buffer[64];
    if (bytes < 1024) {
        swprintf_s(buffer, L"%llu B", bytes);
    } else if (bytes < 1024ull * 1024ull) {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ull * 1024ull * 1024ull) {
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        swprintf_s(buffer, L"%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buffer;
}

std::wstring Trim(const std::wstring& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && iswspace(text[start])) {
        ++start;
    }
    while (end > start && iswspace(text[end - 1])) {
        --end;
    }
    return text.substr(start, end - start);
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

UINT DpiOfWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn) {
        const UINT dpi = fn(hwnd);
        if (dpi) {
            return dpi;
        }
    }
    return 96;
}

namespace {

using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);

void LoadDarkModeApis() {
    static bool loaded = false;
    static HMODULE uxtheme = nullptr;
    if (loaded) {
        return;
    }
    loaded = true;
    uxtheme = LoadLibraryW(L"uxtheme.dll");
    if (!uxtheme) {
        return;
    }
    auto setPreferred = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    if (setPreferred) {
        // ForceDark. This is the undocumented dark-mode switch used by Explorer.
        setPreferred(2);
    }
}

AllowDarkModeForWindowFn GetAllowDarkMode() {
    static AllowDarkModeForWindowFn fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
        if (uxtheme) {
            fn = reinterpret_cast<AllowDarkModeForWindowFn>(
                GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
        }
    }
    return fn;
}

}  // namespace

void EnableDarkModeForApp() {
    LoadDarkModeApis();
}

void AllowDarkModeForWindow(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    if (auto allow = GetAllowDarkMode()) {
        allow(hwnd, TRUE);
    }
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

namespace {

BOOL CALLBACK DarkenDialogChild(HWND child, LPARAM) {
    AllowDarkModeForWindow(child);
    return TRUE;
}

}  // namespace

void ApplyDarkDialogTheme(HWND dialog) {
    if (!dialog) {
        return;
    }
    AllowDarkModeForWindow(dialog);
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(dialog, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(dialog, 19, &dark, sizeof(dark));
    }
    EnumChildWindows(dialog, DarkenDialogChild, 0);
}

void ShowError(HWND owner, const std::wstring& message) {
    MessageBoxW(owner, message.c_str(), L"67t", MB_OK | MB_ICONERROR);
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
        DWORD got = 0;
        if (!ReadFile(file, out.data() + offset, static_cast<DWORD>(std::min<size_t>(out.size() - offset, 1u << 20)), &got, nullptr) || got == 0) {
            CloseHandle(file);
            return false;
        }
        offset += got;
    }
    CloseHandle(file);
    return true;
}

bool WriteTextFileAtomic(const std::wstring& path, std::string_view data, std::wstring& error) {
    const std::wstring dir = DirectoryOf(path);
    EnsureDirectory(dir);
    wchar_t temp[MAX_PATH] = {};
    if (GetTempFileNameW(dir.c_str(), L"67s", 0, temp) == 0) {
        error = GetErrorText(L"Cannot create temporary state file", GetLastError());
        return false;
    }
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(temp);
        error = GetErrorText(L"Cannot write state file", GetLastError());
        return false;
    }
    bool ok = true;
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD written = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
        if (!WriteFile(file, data.data() + offset, want, &written, nullptr) || written == 0) {
            error = GetErrorText(L"Cannot write state file", GetLastError());
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp);
        return false;
    }
    if (!MoveFileExW(temp, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = GetErrorText(L"Cannot replace state file", GetLastError());
        DeleteFileW(temp);
        return false;
    }
    return true;
}

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
