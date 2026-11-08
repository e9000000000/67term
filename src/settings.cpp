#include "settings.h"

#include <algorithm>
#include <string>

#include "util.h"

namespace {

constexpr wchar_t kSection[] = L"67t";

int ReadInt(const std::wstring& path, const wchar_t* key, int fallback) {
    wchar_t buffer[128]{};
    GetPrivateProfileStringW(kSection, key, L"", buffer, static_cast<DWORD>(std::size(buffer)),
                             path.c_str());
    const std::wstring value = buffer;
    if (value.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const long number = wcstol(value.c_str(), &end, 10);
    if (!end || *end != 0) {
        return fallback;
    }
    return static_cast<int>(number);
}

std::wstring ReadString(const std::wstring& path, const wchar_t* key, const wchar_t* fallback) {
    wchar_t buffer[1024]{};
    GetPrivateProfileStringW(kSection, key, fallback, buffer, static_cast<DWORD>(std::size(buffer)),
                             path.c_str());
    return buffer;
}

void WriteValue(const std::wstring& path, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str());
}

}  // namespace

std::wstring SettingsPath() {
    return DataDirectory() + L"\\settings.ini";
}

bool LoadSettings(Settings& settings) {
    const std::wstring path = SettingsPath();
    if (!FileExists(path)) {
        return false;
    }

    settings.fontFamily = ReadString(path, L"font", L"Cascadia Mono");
    if (settings.fontFamily.empty()) {
        settings.fontFamily = L"Cascadia Mono";
    }
    settings.fontSize = std::clamp(ReadInt(path, L"fontSize", 16), 8, 72);
    settings.uiFontFamily = ReadString(path, L"uiFont", L"Segoe UI");
    if (settings.uiFontFamily.empty()) {
        settings.uiFontFamily = L"Segoe UI";
    }
    settings.uiFontSize = std::clamp(ReadInt(path, L"uiFontSize", 9), 6, 36);
    settings.shell = ReadString(path, L"shell", L"");
    settings.scrollback = std::clamp(ReadInt(path, L"scrollback", 10000), 0, 100000);
    settings.cursorBlink = ReadInt(path, L"cursorBlink", 1) != 0;

    const int left = ReadInt(path, L"windowLeft", INT_MIN);
    const int top = ReadInt(path, L"windowTop", INT_MIN);
    const int width = ReadInt(path, L"windowWidth", 0);
    const int height = ReadInt(path, L"windowHeight", 0);
    if (left != INT_MIN && top != INT_MIN && width >= 400 && height >= 260) {
        settings.window = RECT{left, top, left + width, top + height};
        settings.hasWindow = true;
    }
    settings.maximized = ReadInt(path, L"maximized", 0) != 0;
    return true;
}

bool SaveSettings(const Settings& settings) {
    const std::wstring directory = DataDirectory();
    if (!EnsureDirectory(directory)) {
        return false;
    }
    const std::wstring path = SettingsPath();
    const std::wstring temp = path + L".tmp";

    DeleteFileW(temp.c_str());
    WriteValue(temp, L"font", settings.fontFamily);
    WriteValue(temp, L"fontSize", std::to_wstring(settings.fontSize));
    WriteValue(temp, L"uiFont", settings.uiFontFamily);
    WriteValue(temp, L"uiFontSize", std::to_wstring(settings.uiFontSize));
    WriteValue(temp, L"shell", settings.shell);
    WriteValue(temp, L"scrollback", std::to_wstring(settings.scrollback));
    WriteValue(temp, L"cursorBlink", settings.cursorBlink ? L"1" : L"0");
    if (settings.hasWindow) {
        WriteValue(temp, L"windowLeft", std::to_wstring(settings.window.left));
        WriteValue(temp, L"windowTop", std::to_wstring(settings.window.top));
        WriteValue(temp, L"windowWidth",
                   std::to_wstring(settings.window.right - settings.window.left));
        WriteValue(temp, L"windowHeight",
                   std::to_wstring(settings.window.bottom - settings.window.top));
    }
    WriteValue(temp, L"maximized", settings.maximized ? L"1" : L"0");

    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}
