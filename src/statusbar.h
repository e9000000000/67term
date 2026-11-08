#pragma once

#include <windows.h>

#include <string>

struct StatusInfo {
    std::wstring position = L"Ln 1, Col 1";
    std::wstring size = L"80x24";
    std::wstring scrollback;
    std::wstring shell;
    std::wstring mode;
    std::wstring message;
    std::wstring right;
};

class StatusBar {
public:
    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }
    void setInfo(const StatusInfo& info);
    void layout(int x, int y, int width, int height);
    int preferredHeight(HWND reference) const;
    void invalidate();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ensureFont();
    void paint(HDC dc);

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    StatusInfo info_;
};
