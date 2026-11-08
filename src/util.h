#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

std::wstring GetLastErrorText(DWORD error);
std::wstring GetErrorText(const std::wstring& context, DWORD error);

std::wstring ExeDirectory();
std::wstring DataDirectory();
bool EnsureDirectory(const std::wstring& path);

std::wstring FullPath(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);
std::wstring CompactPathLabel(const std::wstring& path);
bool PathsEqual(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& path);

std::wstring FormatBytes(unsigned long long bytes);
std::wstring Trim(const std::wstring& text);

int ScaleForDpi(int value, UINT dpi);
UINT DpiOfWindow(HWND hwnd);

void ShowError(HWND owner, const std::wstring& message);

void EnableDarkModeForApp();
void AllowDarkModeForWindow(HWND hwnd);
void ApplyDarkDialogTheme(HWND dialog);

bool ReadTextFile(const std::wstring& path, std::string& out);
bool WriteTextFileAtomic(const std::wstring& path, std::string_view data, std::wstring& error);

std::wstring ToLower(std::wstring text);
std::string ToLowerAscii(std::string text);
