#pragma once

#include <string>
#include <vector>

struct LaunchOptions {
    std::wstring shell;
    std::wstring cwd;
    std::wstring title;
    std::wstring logPath;
    bool selfTest = false;
    bool bench = false;
    bool benchStartup = false;
    bool setDefaultTerminal = false;
    bool restoreDefaultTerminal = false;
    bool newWindow = false;
    bool help = false;
    bool version = false;
    int benchLines = 10000;
    std::wstring error;
};

LaunchOptions ParseOptions(const std::vector<std::wstring>& arguments);
std::wstring UsageText();
