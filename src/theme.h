#pragma once

#include <windows.h>
#include <algorithm>

namespace theme {

// Flat dark palette. The editor and active tab share the darkest background so
// the title bar reads as part of the document surface.
constexpr COLORREF kBg = RGB(9, 9, 9);
constexpr COLORREF kPanel = RGB(15, 15, 15);
constexpr COLORREF kPanelAlt = RGB(23, 23, 23);
constexpr COLORREF kTabActive = RGB(9, 9, 9);
constexpr COLORREF kTabInactive = RGB(19, 19, 19);
constexpr COLORREF kTabHover = RGB(30, 30, 30);
constexpr COLORREF kBorder = RGB(36, 36, 36);
constexpr COLORREF kText = RGB(216, 216, 216);
constexpr COLORREF kTextDim = RGB(146, 146, 146);
constexpr COLORREF kTextFaint = RGB(98, 98, 98);
constexpr COLORREF kAccent = RGB(43, 107, 154);
constexpr COLORREF kSelection = RGB(29, 58, 85);
constexpr COLORREF kCaretLine = RGB(15, 15, 15);
constexpr COLORREF kMargin = RGB(11, 11, 11);
constexpr COLORREF kMarginText = RGB(112, 112, 112);
constexpr COLORREF kInput = RGB(21, 21, 21);
constexpr COLORREF kDanger = RGB(224, 82, 82);

constexpr int kMenuHeight = 30;
constexpr int kTabHeight = 28;
constexpr int kStatusHeight = 24;
constexpr int kTabWidth = 190;
constexpr int kTabMinWidth = 96;
constexpr int kTabMaxWidth = 240;

// Terminal surface.
constexpr COLORREF kTermFg = RGB(204, 204, 204);
constexpr COLORREF kTermBg = RGB(9, 9, 11);
constexpr COLORREF kTermSelection = RGB(31, 63, 94);
constexpr COLORREF kTermSearch = RGB(34, 52, 46);
constexpr COLORREF kTermSearchCurrent = RGB(74, 112, 80);
constexpr COLORREF kSearchBg = RGB(20, 22, 26);
constexpr COLORREF kTermCursor = RGB(220, 220, 222);
constexpr COLORREF kTermCursorText = RGB(12, 12, 14);
constexpr COLORREF kScrollThumb = RGB(80, 80, 88);
constexpr COLORREF kScrollTrack = RGB(15, 15, 17);
constexpr COLORREF kTermBell = RGB(43, 107, 154);

inline COLORREF AnsiColor(int index) {
    static constexpr COLORREF colors[16] = {
        RGB(12, 12, 12),    RGB(197, 15, 31),   RGB(19, 161, 14),  RGB(193, 156, 0),
        RGB(0, 55, 218),    RGB(136, 23, 152), RGB(58, 150, 221), RGB(204, 204, 204),
        RGB(118, 118, 118), RGB(231, 72, 86),  RGB(22, 198, 12),  RGB(249, 241, 165),
        RGB(59, 120, 255),  RGB(180, 0, 158),  RGB(97, 214, 214), RGB(242, 242, 242),
    };
    return colors[index < 0 ? 0 : (index > 15 ? 15 : index)];
}

inline int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

inline UINT DpiOf(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd) {
        const UINT dpi = fn(hwnd);
        if (dpi) {
            return dpi;
        }
    }
    return 96;
}

}  // namespace theme
