#include "statusbar.h"

#include <algorithm>

#include "theme.h"
#include "util.h"

namespace {

constexpr wchar_t kStatusBarClass[] = L"67tStatusBar";

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

}  // namespace

bool StatusBar::create(HWND parent, HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kStatusBarClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    hwnd_ = CreateWindowExW(0, kStatusBarClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = DpiOfWindow(parent);
    ensureFont();
    return true;
}

void StatusBar::ensureFont() {
    const UINT dpi = DpiOfWindow(hwnd_);
    if (font_ && dpi == dpi_) {
        return;
    }
    dpi_ = dpi;
    if (font_) {
        DeleteObject(font_);
    }
    font_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void StatusBar::setInfo(const StatusInfo& info) {
    info_ = info;
    invalidate();
}

void StatusBar::layout(int x, int y, int width, int height) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        dpi_ = DpiOfWindow(hwnd_);
        ensureFont();
        invalidate();
    }
}

int StatusBar::preferredHeight(HWND reference) const {
    return theme::Scale(theme::kStatusHeight, DpiOfWindow(reference));
}

void StatusBar::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void StatusBar::paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    Fill(dc, client, theme::kPanel);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, font_);

    RECT topLine{0, 0, width, theme::Scale(1, dpi_)};
    Fill(dc, topLine, theme::kBorder);

    const int pad = theme::Scale(10, dpi_);
    int x = pad;
    const int mid = height / 2;

    const auto drawField = [&](const std::wstring& text, COLORREF color, bool separator) {
        if (text.empty()) {
            return;
        }
        SetTextColor(dc, color);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
        RECT textRect{x, 0, x + size.cx + 2, height};
        DrawTextW(dc, text.c_str(), -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        x += size.cx + theme::Scale(14, dpi_);
        if (separator && x < width - pad) {
            RECT line{x - theme::Scale(7, dpi_), mid - theme::Scale(6, dpi_),
                      x - theme::Scale(6, dpi_), mid + theme::Scale(6, dpi_)};
            Fill(dc, line, theme::kBorder);
        }
    };

    if (!info_.message.empty()) {
        drawField(info_.message, theme::kTextDim, false);
    } else {
        drawField(info_.position, theme::kText, true);
        drawField(info_.size, theme::kTextDim, true);
        drawField(info_.scrollback, theme::kTextDim, true);
        drawField(info_.shell, theme::kTextDim, true);
        drawField(info_.mode, theme::kTextFaint, false);
    }

    if (!info_.right.empty()) {
        SetTextColor(dc, theme::kTextFaint);
        SIZE size{};
        GetTextExtentPoint32W(dc, info_.right.c_str(), static_cast<int>(info_.right.size()), &size);
        RECT textRect{width - pad - size.cx, 0, width - pad, height};
        DrawTextW(dc, info_.right.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }

    SelectObject(dc, oldFont);
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK StatusBar::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    StatusBar* self = reinterpret_cast<StatusBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<StatusBar*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->hwnd_ = hwnd;
        }
    }
    if (self) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT StatusBar::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        paint(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wParam);
        ensureFont();
        invalidate();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
