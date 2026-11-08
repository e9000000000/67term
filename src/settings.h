#pragma once

#include <windows.h>

#include <string>

struct Settings {
    std::wstring fontFamily = L"Cascadia Mono";
    int fontSize = 16;
    // Interface font for the title/tabs and menus, separate from the terminal.
    std::wstring uiFontFamily = L"Segoe UI";
    int uiFontSize = 9;
    std::wstring shell;  // empty = automatic (pwsh, powershell, cmd)
    int scrollback = 10000;
    bool cursorBlink = true;
    RECT window{};
    bool hasWindow = false;
    bool maximized = false;
};

std::wstring SettingsPath();
bool LoadSettings(Settings& settings);
bool SaveSettings(const Settings& settings);
