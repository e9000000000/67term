#include <windows.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#include "input.h"
#include "theme.h"
#include "view.h"

namespace {

constexpr wchar_t kViewClass[] = L"67tTerminalView";
constexpr UINT_PTR kBlinkTimer = 1;
constexpr UINT kBlinkIntervalMs = 530;

void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
    const int percentA = 100 - percentB;
    const int r = (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100;
    const int g = (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100;
    const int bl = (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100;
    return RGB(r, g, bl);
}

COLORREF PaletteColor(uint8_t index) {
    if (index < 16) {
        return theme::AnsiColor(index);
    }
    if (index >= 232) {
        const int level = 8 + (index - 232) * 10;
        return RGB(level, level, level);
    }
    const int value = index - 16;
    static constexpr int levels[6] = {0, 95, 135, 175, 215, 255};
    return RGB(levels[value / 36], levels[(value / 6) % 6], levels[value % 6]);
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }
    EmptyClipboard();
    bool ok = false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* destination = GlobalLock(memory)) {
            std::memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory)) {
                ok = true;
            } else {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    return ok;
}

std::wstring GetClipboardText(HWND owner) {
    std::wstring result;
    if (!OpenClipboard(owner)) {
        return result;
    }
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
            result.assign(text);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

int CALLBACK FontExistsCallback(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    *reinterpret_cast<bool*>(lParam) = true;
    return 0;
}

bool FontExists(const std::wstring& family) {
    LOGFONTW font{};
    font.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(font.lfFaceName, family.c_str(), LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &font, FontExistsCallback, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

std::wstring ChooseFontFamily(const std::wstring& requested) {
    if (!requested.empty() && FontExists(requested)) {
        return requested;
    }
    for (const wchar_t* fallback : {L"Cascadia Mono", L"Consolas", L"Lucida Console",
                                    L"Courier New"}) {
        if (FontExists(fallback)) {
            return fallback;
        }
    }
    return L"Consolas";
}

}  // namespace

TerminalView::TerminalView() = default;

TerminalView::~TerminalView() {
    if (hwnd_) {
        KillTimer(hwnd_, kBlinkTimer);
    }
    session_.close();
    for (HFONT* font : {&regularFont_, &boldFont_, &italicFont_, &boldItalicFont_}) {
        if (*font) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

bool TerminalView::create(HWND parent, HINSTANCE instance) {
    parent_ = parent;
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.lpszClassName = kViewClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(0, kViewClass, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    dpi_ = theme::DpiOf(hwnd_ ? hwnd_ : parent);
    ensureFonts();
    measure();
    SetTimer(hwnd_, kBlinkTimer, kBlinkIntervalMs, nullptr);
    return true;
}

void TerminalView::layout(int x, int y, int width, int height) {
    if (!hwnd_) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    measure();
    applySize();
}

void TerminalView::show(bool visible) {
    if (hwnd_) {
        ShowWindow(hwnd_, visible ? SW_SHOWNA : SW_HIDE);
    }
}

void TerminalView::focus() {
    if (hwnd_) {
        SetFocus(hwnd_);
    }
}

void TerminalView::invalidate() {
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TerminalView::setAppearance(const std::wstring& fontFamily, int fontPointSize,
                                 bool cursorBlink, int scrollback) {
    fontFamily_ = fontFamily;
    fontPointSize_ = std::clamp(fontPointSize, 6, 72);
    blinkSetting_ = cursorBlink;
    scrollbackLimit_ = std::clamp(scrollback, 0, 100000);
    terminal_.setScrollbackLimit(scrollbackLimit_);
    ensureFonts();
    measure();
    applySize();
    invalidate();
}

void TerminalView::setScrollbackLimit(int lines) {
    scrollbackLimit_ = std::clamp(lines, 0, 100000);
    terminal_.setScrollbackLimit(scrollbackLimit_);
    notifyParent(kViewStateChanged);
}

void TerminalView::applyDpi(UINT dpi) {
    if (dpi == dpi_) {
        return;
    }
    dpi_ = dpi;
    ensureFonts();
    measure();
    applySize();
    invalidate();
}

void TerminalView::ensureFonts() {
    const UINT dpi = hwnd_ ? theme::DpiOf(hwnd_) : dpi_;
    dpi_ = dpi ? dpi : 96;
    for (HFONT* font : {&regularFont_, &boldFont_, &italicFont_, &boldItalicFont_}) {
        if (*font) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }

    const std::wstring family = ChooseFontFamily(fontFamily_);
    const int height = -MulDiv(fontPointSize_, static_cast<int>(dpi_), 72);
    const auto make = [&](int weight, bool italic) {
        return CreateFontW(height, 0, 0, 0, weight, FALSE, italic, FALSE, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FIXED_PITCH | FF_MODERN, family.c_str());
    };
    regularFont_ = make(FW_NORMAL, false);
    boldFont_ = make(FW_BOLD, false);
    italicFont_ = make(FW_NORMAL, true);
    boldItalicFont_ = make(FW_BOLD, true);

    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, regularFont_);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SIZE size{};
    GetTextExtentPoint32W(dc, L"M", 1, &size);
    cellWidth_ = std::max(1, static_cast<int>(size.cx));
    cellHeight_ = std::max(1, static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading));
    ascent_ = static_cast<int>(metrics.tmAscent);
    textHeight_ = std::max(1, static_cast<int>(metrics.tmHeight));
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);

    padding_ = std::max(4, theme::Scale(7, dpi_));
    scrollbarWidth_ = std::max(3, theme::Scale(5, dpi_));
}

void TerminalView::measure() {
    if (!hwnd_) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int rightInset = scrollbarWidth_ + theme::Scale(6, dpi_);
    const int usableWidth = std::max(1, width - padding_ * 2 - rightInset);
    const int usableHeight = std::max(1, height - padding_ * 2);
    columns_ = std::max(2, usableWidth / std::max(1, cellWidth_));
    rows_ = std::max(1, usableHeight / std::max(1, cellHeight_));
    terminal_.setPixelSize(columns_ * cellWidth_, rows_ * cellHeight_);
}

void TerminalView::applySize() {
    const bool changed = terminal_.columns() != columns_ || terminal_.rows() != rows_;
    if (changed) {
        terminal_.resize(columns_, rows_);
        session_.resize(columns_, rows_);
        notifyParent(kViewStateChanged);
    }
    invalidate();
}

bool TerminalView::start(const std::wstring& commandLine, const std::wstring& workingDirectory,
                         const std::wstring& titleHint) {
    session_.close();
    shellCommand_ = commandLine;
    workingDirectory_ = workingDirectory;
    titleHint_ = titleHint;
    starting_ = true;
    startFailed_ = false;
    exited_ = false;
    haveSelection_ = false;
    selecting_ = false;
    pendingHighSurrogate_ = 0;
    terminal_.setScrollbackLimit(scrollbackLimit_);
    terminal_.reset(true);
    terminal_.resize(columns_, rows_);
    terminal_.setPixelSize(columns_ * cellWidth_, rows_ * cellHeight_);

    terminal_.setResponseCallback([this](std::string text) { session_.write(text); });
    terminal_.setTitleCallback([this](const std::wstring&) {
        notifyParent(kViewTitleChanged);
        invalidate();
    });
    terminal_.setBellCallback([this] {
        bellUntil_ = GetTickCount64() + 160;
        notifyParent(kViewBell);
        invalidate();
    });
    terminal_.setClipboardCallback([this](const std::wstring& text) {
        SetClipboardText(hwnd_, text);
    });
    terminal_.setDirectoryCallback([this](const std::wstring&) {
        notifyParent(kViewStateChanged);
    });

    std::wstring error;
    const bool ok = session_.start(commandLine, workingDirectory, columns_, rows_, hwnd_, error);
    starting_ = false;
    if (!ok) {
        startFailed_ = true;
        exited_ = true;
        const std::wstring message = L"\x1b[31m" + error + L"\x1b[0m\r\n";
        std::string utf8;
        const int length = WideCharToMultiByte(CP_UTF8, 0, message.c_str(),
                                               static_cast<int>(message.size()), nullptr, 0,
                                               nullptr, nullptr);
        if (length > 0) {
            utf8.resize(static_cast<size_t>(length));
            WideCharToMultiByte(CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()),
                                utf8.data(), length, nullptr, nullptr);
            terminal_.feed(utf8);
        }
        notifyParent(kViewExited);
        invalidate();
        return false;
    }

    notifyParent(kViewTitleChanged);
    notifyParent(kViewStateChanged);
    invalidate();
    return true;
}

void TerminalView::restart() {
    if (shellCommand_.empty()) {
        return;
    }
    start(shellCommand_, workingDirectory_, titleHint_);
}

void TerminalView::closeSession() {
    session_.close();
    starting_ = false;
    exited_ = true;
}

void TerminalView::feedFromSession() {
    std::string data;
    if (!session_.drain(data) || data.empty()) {
        return;
    }
    terminal_.feed(data);
    invalidate();
}

void TerminalView::onSessionExit() {
    std::string data;
    if (session_.drain(data) && !data.empty()) {
        terminal_.feed(data);
    }
    const bool wasExited = exited_;
    exited_ = true;
    starting_ = false;
    if (!wasExited) {
        terminal_.feed("\r\n\x1b[38;5;244m[process exited - Ctrl+Shift+R to restart]\x1b[0m\r\n");
        notifyParent(kViewExited);
    }
    invalidate();
}

const std::wstring& TerminalView::title() const {
    if (!terminal_.title().empty()) {
        return terminal_.title();
    }
    return titleHint_;
}

// ---------------------------------------------------------------- paint

COLORREF TerminalView::resolveColor(const vt::Color& color, bool foreground) const {
    if (color.kind == vt::Color::Indexed) {
        return PaletteColor(color.index);
    }
    if (color.kind == vt::Color::Rgb) {
        return RGB(color.r, color.g, color.b);
    }
    if (foreground) {
        return terminal_.reverseVideo() ? theme::kTermBg : theme::kTermFg;
    }
    return terminal_.reverseVideo() ? theme::kTermFg : theme::kTermBg;
}

COLORREF TerminalView::cellBackground(const vt::Cell& cell) const {
    COLORREF fg = resolveColor(cell.fg, true);
    COLORREF bg = resolveColor(cell.bg, false);
    if (cell.attrs & vt::AttrInverse) {
        std::swap(fg, bg);
    }
    (void)fg;
    return bg;
}

COLORREF TerminalView::cellForeground(const vt::Cell& cell) const {
    COLORREF fg = resolveColor(cell.fg, true);
    COLORREF bg = resolveColor(cell.bg, false);
    if (cell.attrs & vt::AttrInverse) {
        std::swap(fg, bg);
    }
    if (cell.attrs & vt::AttrInvisible) {
        return bg;
    }
    if (cell.attrs & vt::AttrDim) {
        fg = Blend(fg, bg, 45);
    }
    return fg;
}

bool TerminalView::cellSelected(int x, int y) const {
    if (!haveSelection_) {
        return false;
    }
    if (y < selectionStartY_ || y > selectionEndY_) {
        return false;
    }
    if (selectionStartY_ == selectionEndY_) {
        return y == selectionStartY_ && x >= selectionStartX_ && x <= selectionEndX_;
    }
    if (y == selectionStartY_) {
        return x >= selectionStartX_;
    }
    if (y == selectionEndY_) {
        return x <= selectionEndX_;
    }
    return true;
}

void TerminalView::paint(HDC target) {
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

    Fill(dc, client, theme::kTermBg);
    paintGrid(dc, client);
    paintCursor(dc);
    paintScrollbar(dc, client);

    if (bellUntil_ > GetTickCount64()) {
        RECT border{0, 0, width, height};
        HBRUSH brush = CreateSolidBrush(theme::kTermBell);
        FrameRect(dc, &border, brush);
        DeleteObject(brush);
    }

    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void TerminalView::paintGrid(HDC dc, const RECT&) {
    SetBkMode(dc, TRANSPARENT);
    const int left = padding_;
    const int top = padding_;
    const int gridWidth = columns_ * cellWidth_;
    const int gridHeight = rows_ * cellHeight_;

    for (int row = 0; row < rows_; ++row) {
        const vt::Line* line = terminal_.viewLine(row);
        const int rowTop = top + row * cellHeight_;

        // Backgrounds, batched by color.
        int x = 0;
        while (x < columns_) {
            const vt::Cell& cell =
                (line && x < static_cast<int>(line->cells.size())) ? line->cells[x] : vt::Cell{};
            COLORREF color = cellSelected(x, row) ? theme::kTermSelection : cellBackground(cell);
            int end = x + 1;
            while (end < columns_) {
                const vt::Cell& next =
                    (line && end < static_cast<int>(line->cells.size())) ? line->cells[end]
                                                                         : vt::Cell{};
                COLORREF nextColor =
                    cellSelected(end, row) ? theme::kTermSelection : cellBackground(next);
                if (nextColor != color) {
                    break;
                }
                ++end;
            }
            if (color != theme::kTermBg) {
                RECT rect{left + x * cellWidth_, rowTop, left + end * cellWidth_,
                          rowTop + cellHeight_};
                Fill(dc, rect, color);
            }
            x = end;
        }

        // Text runs.
        HGDIOBJ oldFont = nullptr;
        HFONT activeFont = nullptr;
        COLORREF activeColor = CLR_INVALID;
        std::wstring run;
        int runX = 0;
        int runEndX = 0;
        auto flush = [&]() {
            if (run.empty()) {
                return;
            }
            if (!oldFont && activeFont) {
                oldFont = SelectObject(dc, activeFont);
            }
            SetTextColor(dc, activeColor);
            RECT clip{left + runX * cellWidth_, rowTop, left + runEndX * cellWidth_,
                      rowTop + cellHeight_};
            ExtTextOutW(dc, clip.left, rowTop + (cellHeight_ - textHeight_) / 2, ETO_CLIPPED, &clip,
                        run.c_str(), static_cast<UINT>(run.size()), nullptr);
            run.clear();
        };

        x = 0;
        while (x < columns_) {
            const vt::Cell& cell =
                (line && x < static_cast<int>(line->cells.size())) ? line->cells[x] : vt::Cell{};
            if (cell.width == 0) {
                ++x;
                continue;
            }
            const bool blinking = (cell.attrs & vt::AttrBlink) && !blinkPhase_;
            if (cell.empty() || blinking) {
                flush();
                x += cell.width == 0 ? 1 : cell.width;
                continue;
            }
            HFONT font = regularFont_;
            if ((cell.attrs & vt::AttrBold) && (cell.attrs & vt::AttrItalic)) {
                font = boldItalicFont_;
            } else if (cell.attrs & vt::AttrBold) {
                font = boldFont_;
            } else if (cell.attrs & vt::AttrItalic) {
                font = italicFont_;
            }
            const COLORREF color = cellForeground(cell);
            if (cell.width == 2) {
                flush();
                HGDIOBJ saved = SelectObject(dc, font);
                SetTextColor(dc, color);
                RECT clip{left + x * cellWidth_, rowTop,
                          left + (x + 2) * cellWidth_, rowTop + cellHeight_};
                ExtTextOutW(dc, clip.left, rowTop + (cellHeight_ - textHeight_) / 2, ETO_CLIPPED,
                            &clip, cell.text, static_cast<UINT>(wcslen(cell.text)), nullptr);
                SelectObject(dc, saved);
                x += 2;
                continue;
            }
            if (font != activeFont || color != activeColor) {
                flush();
                activeFont = font;
                activeColor = color;
                runX = x;
                if (oldFont) {
                    SelectObject(dc, oldFont);
                    oldFont = nullptr;
                }
            }
            run.append(cell.text);
            runEndX = x + 1;
            ++x;
        }
        flush();
        if (oldFont) {
            SelectObject(dc, oldFont);
        }

        // Decorations.
        x = 0;
        while (x < columns_) {
            const vt::Cell& cell =
                (line && x < static_cast<int>(line->cells.size())) ? line->cells[x] : vt::Cell{};
            if (cell.width != 0 && (cell.attrs & (vt::AttrUnderline | vt::AttrStrike))) {
                const int cellLeft = left + x * cellWidth_;
                const int cellRight = cellLeft + (cell.width == 2 ? 2 : 1) * cellWidth_;
                if (cell.attrs & vt::AttrUnderline) {
                    const int y = rowTop + cellHeight_ - std::max(1, theme::Scale(1, dpi_));
                    HPEN pen = CreatePen(PS_SOLID, std::max(1, theme::Scale(1, dpi_)),
                                         cellForeground(cell));
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    MoveToEx(dc, cellLeft, y, nullptr);
                    LineTo(dc, cellRight, y);
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                }
                if (cell.attrs & vt::AttrStrike) {
                    const int y = rowTop + cellHeight_ / 2;
                    HPEN pen = CreatePen(PS_SOLID, std::max(1, theme::Scale(1, dpi_)),
                                         cellForeground(cell));
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    MoveToEx(dc, cellLeft, y, nullptr);
                    LineTo(dc, cellRight, y);
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                }
            }
            x += cell.width == 2 ? 2 : 1;
        }
    }
    (void)gridWidth;
    (void)gridHeight;
}

void TerminalView::paintCursor(HDC dc) {
    if (!focused_ || !cursorOn_ || terminal_.scrollOffset() != 0 || !terminal_.cursorVisible()) {
        return;
    }
    const int x = terminal_.cursorX();
    const int y = terminal_.cursorY();
    if (x < 0 || x >= columns_ || y < 0 || y >= rows_) {
        return;
    }
    const int cellLeft = padding_ + x * cellWidth_;
    const int cellTop = padding_ + y * cellHeight_;
    RECT rect{cellLeft, cellTop, cellLeft + cellWidth_, cellTop + cellHeight_};

    if (terminal_.cursorShape() == vt::CursorShape::Bar) {
        rect.right = rect.left + std::max(1, theme::Scale(2, dpi_));
        Fill(dc, rect, theme::kTermCursor);
        return;
    }
    if (terminal_.cursorShape() == vt::CursorShape::Underline) {
        rect.top = rect.bottom - std::max(1, theme::Scale(2, dpi_));
        Fill(dc, rect, theme::kTermCursor);
        return;
    }

    Fill(dc, rect, theme::kTermCursor);
    const vt::Cell& cell = terminal_.viewCell(x, y);
    if (!cell.empty() && cell.width != 0) {
        HGDIOBJ oldFont = SelectObject(dc, regularFont_);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme::kTermCursorText);
        RECT clip = rect;
        if (cell.width == 2) {
            clip.right += cellWidth_;
        }
        ExtTextOutW(dc, clip.left, clip.top, ETO_CLIPPED, &clip, cell.text,
                    static_cast<UINT>(wcslen(cell.text)), nullptr);
        SelectObject(dc, oldFont);
    }
}

void TerminalView::paintScrollbar(HDC dc, const RECT& client) {
    const int maxOffset = terminal_.maxScrollOffset();
    if (maxOffset <= 0) {
        return;
    }
    const int trackTop = padding_;
    const int trackBottom = client.bottom - padding_;
    const int trackHeight = std::max(1, trackBottom - trackTop);
    const int x = client.right - padding_ - scrollbarWidth_;
    RECT track{x, trackTop, x + scrollbarWidth_, trackBottom};
    Fill(dc, track, theme::kScrollTrack);

    const double visibleRatio =
        static_cast<double>(rows_) / static_cast<double>(rows_ + maxOffset);
    const int thumbHeight =
        std::max(theme::Scale(12, dpi_), static_cast<int>(trackHeight * visibleRatio));
    const int travel = std::max(0, trackHeight - thumbHeight);
    const double position = 1.0 - static_cast<double>(terminal_.scrollOffset()) / maxOffset;
    const int thumbTop = trackTop + static_cast<int>(travel * position + 0.5);
    RECT thumb{x, thumbTop, x + scrollbarWidth_, std::min(trackBottom, thumbTop + thumbHeight)};
    Fill(dc, thumb, theme::kScrollThumb);
}

// ---------------------------------------------------------------- selection

void TerminalView::focusCellFromPoint(int px, int py, int& x, int& y) const {
    x = (px - padding_) / std::max(1, cellWidth_);
    y = (py - padding_) / std::max(1, cellHeight_);
    x = std::clamp(x, 0, columns_ - 1);
    y = std::clamp(y, 0, rows_ - 1);
}

void TerminalView::beginSelection(int x, int y) {
    selectionAnchorX_ = x;
    selectionAnchorY_ = y;
    selectionStartX_ = x;
    selectionStartY_ = y;
    selectionEndX_ = x;
    selectionEndY_ = y;
    haveSelection_ = false;
    selecting_ = true;
}

void TerminalView::updateSelection(int x, int y) {
    if (!selecting_) {
        return;
    }
    const bool same = x == selectionAnchorX_ && y == selectionAnchorY_;
    if (same && !haveSelection_) {
        return;
    }
    haveSelection_ = true;
    const bool forward =
        (y > selectionAnchorY_) || (y == selectionAnchorY_ && x >= selectionAnchorX_);
    if (forward) {
        selectionStartX_ = selectionAnchorX_;
        selectionStartY_ = selectionAnchorY_;
        selectionEndX_ = x;
        selectionEndY_ = y;
    } else {
        selectionStartX_ = x;
        selectionStartY_ = y;
        selectionEndX_ = selectionAnchorX_;
        selectionEndY_ = selectionAnchorY_;
    }
    invalidate();
}

void TerminalView::endSelection() {
    selecting_ = false;
    notifyParent(kViewStateChanged);
    invalidate();
}

void TerminalView::selectWordAt(int x, int y) {
    const vt::Line* line = terminal_.viewLine(y);
    if (!line) {
        return;
    }
    auto isWord = [](const vt::Cell& cell) {
        if (cell.width == 0 || cell.text[0] == 0) {
            return false;
        }
        return std::iswalnum(cell.text[0]) || cell.text[0] == L'_' || cell.text[0] == L'-' ||
               cell.text[0] == L'.' || cell.text[0] == L'/' || cell.text[0] == L'\\' ||
               cell.text[0] == L':' || cell.text[0] == L'@';
    };
    int start = x;
    int end = x;
    while (start > 0 && start - 1 < static_cast<int>(line->cells.size()) &&
           isWord(line->cells[static_cast<size_t>(start - 1)])) {
        --start;
    }
    while (end + 1 < static_cast<int>(line->cells.size()) && isWord(line->cells[static_cast<size_t>(end + 1)])) {
        ++end;
    }
    selectionStartX_ = start;
    selectionStartY_ = y;
    selectionEndX_ = end;
    selectionEndY_ = y;
    haveSelection_ = true;
    selecting_ = false;
    notifyParent(kViewStateChanged);
    invalidate();
}

void TerminalView::selectLineAt(int x, int y) {
    (void)x;
    selectionStartX_ = 0;
    selectionStartY_ = y;
    selectionEndX_ = columns_ - 1;
    selectionEndY_ = y;
    haveSelection_ = true;
    selecting_ = false;
    notifyParent(kViewStateChanged);
    invalidate();
}

bool TerminalView::hasSelection() const {
    return haveSelection_;
}

std::wstring TerminalView::selectedText() const {
    if (!haveSelection_) {
        return {};
    }
    std::wstring result;
    for (int y = selectionStartY_; y <= selectionEndY_; ++y) {
        const vt::Line* line = terminal_.viewLine(y);
        if (!line) {
            continue;
        }
        const int from = y == selectionStartY_ ? selectionStartX_ : 0;
        const int to = y == selectionEndY_ ? selectionEndX_ : columns_ - 1;
        std::wstring row;
        for (int x = from; x <= to && x < static_cast<int>(line->cells.size()); ++x) {
            const vt::Cell& cell = line->cells[static_cast<size_t>(x)];
            if (cell.width == 0) {
                continue;
            }
            if (cell.text[0] == 0) {
                row.push_back(L' ');
            } else {
                row.append(cell.text);
            }
        }
        while (!row.empty() && row.back() == L' ') {
            row.pop_back();
        }
        result.append(row);
        if (y != selectionEndY_) {
            result.append(L"\r\n");
        }
    }
    return result;
}

void TerminalView::copySelection() {
    const std::wstring text = selectedText();
    if (!text.empty()) {
        SetClipboardText(hwnd_, text);
        notifyParent(kViewStateChanged);
    }
}

void TerminalView::pasteClipboard() {
    const std::wstring text = GetClipboardText(hwnd_);
    if (!text.empty()) {
        pasteText(text);
    }
}

void TerminalView::pasteText(const std::wstring& text) {
    if (text.empty() || !session_.running()) {
        return;
    }
    const std::wstring normalized = input::NormalizePaste(text);
    if (terminal_.bracketedPaste()) {
        sendUtf16(L"\x1b[200~");
        sendUtf16(normalized);
        sendUtf16(L"\x1b[201~");
    } else {
        sendUtf16(normalized);
    }
    terminal_.setScrollOffset(0);
    invalidate();
}

void TerminalView::selectAll() {
    selectionStartX_ = 0;
    selectionStartY_ = 0;
    selectionEndX_ = columns_ - 1;
    selectionEndY_ = rows_ - 1;
    haveSelection_ = true;
    notifyParent(kViewStateChanged);
    invalidate();
}

void TerminalView::clearSelection() {
    haveSelection_ = false;
    selecting_ = false;
    invalidate();
}

void TerminalView::scrollToBottom() {
    terminal_.setScrollOffset(0);
    invalidate();
    notifyParent(kViewStateChanged);
}

void TerminalView::scrollLines(int lines) {
    terminal_.scrollBy(lines);
    invalidate();
    notifyParent(kViewStateChanged);
}

void TerminalView::clearScrollback() {
    terminal_.clearScrollback();
    invalidate();
    notifyParent(kViewStateChanged);
}

void TerminalView::scrollWithDelta(int lines) {
    terminal_.scrollBy(lines);
    invalidate();
    notifyParent(kViewStateChanged);
}

bool TerminalView::pointOnScrollbar(int x, int y) const {
    if (!hwnd_ || terminal_.maxScrollOffset() <= 0) {
        return false;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    return x >= client.right - padding_ - scrollbarWidth_ - theme::Scale(2, dpi_) &&
           x < client.right && y >= padding_ && y < client.bottom - padding_ + cellHeight_;
}

void TerminalView::scrollToPoint(int y) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int trackTop = padding_;
    const int trackHeight = std::max(1, static_cast<int>(client.bottom) - padding_ * 2);
    const int maxOffset = terminal_.maxScrollOffset();
    double fraction = static_cast<double>(y - trackTop) / trackHeight;
    fraction = std::clamp(fraction, 0.0, 1.0);
    terminal_.setScrollOffset(static_cast<int>((1.0 - fraction) * maxOffset + 0.5));
    invalidate();
    notifyParent(kViewStateChanged);
}

// ---------------------------------------------------------------- input

void TerminalView::sendText(std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    if (terminal_.scrollOffset() != 0) {
        terminal_.setScrollOffset(0);
        invalidate();
    }
    session_.write(bytes);
}

void TerminalView::sendUtf16(std::wstring_view text) {
    if (text.empty()) {
        return;
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return;
    }
    std::string bytes(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), needed,
                        nullptr, nullptr);
    sendText(bytes);
}

void TerminalView::writeInput(std::string_view bytes) {
    sendText(bytes);
}

void TerminalView::sendKey(WPARAM key, bool ctrl, bool alt, bool shift, bool) {
    input::KeyEvent event;
    event.key = key;
    event.ctrl = ctrl;
    event.alt = alt;
    event.shift = shift;
    event.applicationCursor = terminal_.applicationCursorKeys();
    std::string sequence;
    if (input::EncodeKey(event, sequence)) {
        sendText(sequence);
    }
}

std::string TerminalView::encodeMouse(int button, int x, int y, bool release, bool motion) const {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const int modifiers = (shift ? 4 : 0) + (alt ? 8 : 0) + (ctrl ? 16 : 0);
    return input::EncodeMouse(button, x, y, release, motion, terminal_.sgrMouse(), modifiers);
}

void TerminalView::sendMouse(WORD button, int x, int y, bool press, bool wheel, int wheelDelta) {
    if (!terminal_.mouseTracking()) {
        return;
    }
    if (wheel) {
        const int code = wheelDelta > 0 ? 64 : 65;
        session_.write(encodeMouse(code, x, y, false, false));
        return;
    }
    mouseButtonDown_ = press;
    if (press) {
        mouseButton_ = button;
    }
    session_.write(encodeMouse(button, x, y, !press, false));
    lastMouseCellX_ = x;
    lastMouseCellY_ = y;
}

void TerminalView::notifyParent(UINT message) {
    if (parent_) {
        SendMessageW(parent_, message, reinterpret_cast<WPARAM>(hwnd_), 0);
    }
}

// ---------------------------------------------------------------- messages

LRESULT CALLBACK TerminalView::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TerminalView* self = reinterpret_cast<TerminalView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<TerminalView*>(create->lpCreateParams);
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

LRESULT TerminalView::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
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
    case WM_SIZE:
        measure();
        applySize();
        return 0;
    case WM_DPICHANGED:
        applyDpi(HIWORD(wParam));
        return 0;
    case WM_SETFOCUS:
        focused_ = true;
        cursorOn_ = true;
        blinkPhase_ = true;
        if (terminal_.focusReporting()) {
            session_.write("\x1b[I");
        }
        invalidate();
        return 0;
    case WM_KILLFOCUS:
        focused_ = false;
        if (terminal_.focusReporting()) {
            session_.write("\x1b[O");
        }
        invalidate();
        return 0;
    case WM_TIMER:
        if (wParam == kBlinkTimer) {
            if (blinkSetting_) {
                blinkPhase_ = !blinkPhase_;
                cursorOn_ = !cursorOn_;
                if (focused_ && IsWindowVisible(hwnd_)) {
                    invalidate();
                }
            } else {
                blinkPhase_ = true;
                cursorOn_ = true;
            }
        }
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case kPtyDataMessage:
        feedFromSession();
        notifyParent(kViewStateChanged);
        return 0;
    case kPtyExitMessage:
        onSessionExit();
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (message == WM_SYSKEYDOWN && wParam == VK_F4) {
            break;
        }
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        switch (wParam) {
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:
        case VK_LWIN:
        case VK_RWIN:
        case VK_CAPITAL:
        case VK_NUMLOCK:
        case VK_SCROLL:
            return 0;
        case VK_ESCAPE:
            sendText("\x1b");
            return 0;
        case VK_SPACE:
            if (ctrl) {
                sendText(std::string(1, '\0'));
                suppressNextChar_ = true;
                return 0;
            }
            break;
        case VK_BACK:
            if (alt) {
                sendText("\x1b\x7f");
                suppressNextChar_ = true;
                return 0;
            }
            break;
        default:
            break;
        }
        if (shift && !ctrl && !alt) {
            if (wParam == VK_PRIOR) {
                scrollWithDelta(std::max(1, rows_ - 1));
                return 0;
            }
            if (wParam == VK_NEXT) {
                scrollWithDelta(-std::max(1, rows_ - 1));
                return 0;
            }
        }
        if (wParam == VK_INSERT && shift) {
            pasteClipboard();
            suppressNextChar_ = true;
            return 0;
        }
        switch (wParam) {
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_HOME:
        case VK_END:
        case VK_INSERT:
        case VK_DELETE:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_F1:
        case VK_F2:
        case VK_F3:
        case VK_F4:
        case VK_F5:
        case VK_F6:
        case VK_F7:
        case VK_F8:
        case VK_F9:
        case VK_F10:
        case VK_F11:
        case VK_F12:
            sendKey(wParam, ctrl, alt, shift, false);
            return 0;
        case VK_TAB:
            if (shift && !ctrl) {
                sendKey(wParam, ctrl, alt, shift, false);
                suppressNextChar_ = true;
                return 0;
            }
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_CHAR: {
        if (suppressNextChar_) {
            suppressNextChar_ = false;
            return 0;
        }
        wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch == 0) {
            return 0;
        }
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            pendingHighSurrogate_ = ch;
            return 0;
        }
        if (ch >= 0xDC00 && ch <= 0xDFFF) {
            if (pendingHighSurrogate_) {
                const wchar_t pair[2] = {pendingHighSurrogate_, ch};
                pendingHighSurrogate_ = 0;
                sendUtf16(std::wstring_view(pair, 2));
            }
            return 0;
        }
        if (pendingHighSurrogate_) {
            pendingHighSurrogate_ = 0;
        }
        if (ch == L'\b') {
            sendText("\x7f");
        } else if (ch == L'\r') {
            sendText("\r");
        } else if (ch == 0x7F) {
            return 0;
        } else {
            sendUtf16(std::wstring_view(&ch, 1));
        }
        return 0;
    }
    case WM_SYSCHAR:
        if (wParam >= 0x20 && wParam != 0x7F) {
            const wchar_t ch = static_cast<wchar_t>(wParam);
            sendText("\x1b");
            sendUtf16(std::wstring_view(&ch, 1));
            return 0;
        }
        return 0;

    case WM_IME_STARTCOMPOSITION: {
        HIMC context = ImmGetContext(hwnd_);
        if (context) {
            COMPOSITIONFORM form{};
            form.dwStyle = CFS_POINT;
            form.ptCurrentPos.x = padding_ + terminal_.cursorX() * cellWidth_;
            form.ptCurrentPos.y = padding_ + terminal_.cursorY() * cellHeight_;
            ImmSetCompositionWindow(context, &form);
            ImmReleaseContext(hwnd_, context);
        }
        return 0;
    }
    case WM_IME_COMPOSITION:
        if (lParam & GCS_RESULTSTR) {
            HIMC context = ImmGetContext(hwnd_);
            if (context) {
                const LONG length = ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
                if (length > 0) {
                    std::wstring text(static_cast<size_t>(length) / sizeof(wchar_t), L'\0');
                    ImmGetCompositionStringW(context, GCS_RESULTSTR, text.data(), length);
                    sendUtf16(text);
                }
                ImmReleaseContext(hwnd_, context);
            }
            return 0;
        }
        return 0;
    case WM_IME_CHAR:
        return 0;

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        int x = 0;
        int y = 0;
        focusCellFromPoint(point.x, point.y, x, y);
        const bool shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
        if (terminal_.mouseTracking() && !shift) {
            sendMouse(0, x, y, true, true, delta);
        } else {
            scrollWithDelta(delta / 120 * 3);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int px = GET_X_LPARAM(lParam);
        const int py = GET_Y_LPARAM(lParam);
        if (scrollbarDragging_) {
            scrollToPoint(py);
            return 0;
        }
        int x = 0;
        int y = 0;
        focusCellFromPoint(px, py, x, y);
        if (selecting_) {
            updateSelection(x, y);
            return 0;
        }
        if (terminal_.mouseTracking() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            const int mode = terminal_.mouseTrackingMode();
            const bool report = mode == 1003 || (mode == 1002 && mouseButtonDown_);
            if (report && (x != lastMouseCellX_ || y != lastMouseCellY_)) {
                session_.write(encodeMouse(mouseButton_, x, y, false, true));
                lastMouseCellX_ = x;
                lastMouseCellY_ = y;
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd_);
        const int px = GET_X_LPARAM(lParam);
        const int py = GET_Y_LPARAM(lParam);
        if (pointOnScrollbar(px, py)) {
            scrollbarDragging_ = true;
            scrollToPoint(py);
            SetCapture(hwnd_);
            return 0;
        }
        int x = 0;
        int y = 0;
        focusCellFromPoint(px, py, x, y);
        if (terminal_.mouseTracking() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            SetCapture(hwnd_);
            sendMouse(0, x, y, true, false, 0);
            return 0;
        }
        const ULONGLONG now = GetTickCount64();
        if (now - lastClickTick_ < 500 && x == lastClickX_ && y == lastClickY_) {
            ++clickCount_;
        } else {
            clickCount_ = 1;
        }
        lastClickTick_ = now;
        lastClickX_ = x;
        lastClickY_ = y;
        if (clickCount_ == 2) {
            selectWordAt(x, y);
        } else if (clickCount_ >= 3) {
            selectLineAt(x, y);
        } else {
            beginSelection(x, y);
        }
        SetCapture(hwnd_);
        invalidate();
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        if (scrollbarDragging_) {
            scrollbarDragging_ = false;
            return 0;
        }
        int x = 0;
        int y = 0;
        focusCellFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);
        if (terminal_.mouseTracking() && mouseButtonDown_ &&
            !(GetKeyState(VK_SHIFT) & 0x8000)) {
            sendMouse(static_cast<WORD>(mouseButton_), x, y, false, false, 0);
            return 0;
        }
        if (selecting_) {
            endSelection();
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = 0;
        int y = 0;
        focusCellFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);
        if (!terminal_.mouseTracking() || (GetKeyState(VK_SHIFT) & 0x8000)) {
            selectWordAt(x, y);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        if (terminal_.mouseTracking() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            int x = 0;
            int y = 0;
            focusCellFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), x, y);
            session_.write(encodeMouse(2, x, y, false, false));
            session_.write(encodeMouse(2, x, y, true, false));
            return 0;
        }
        break;
    }
    case WM_CONTEXTMENU: {
        if (terminal_.mouseTracking() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
            return 0;
        }
        POINT point{};
        if (lParam == static_cast<LPARAM>(-1)) {
            GetCursorPos(&point);
        } else {
            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);
        }
        if (parent_) {
            SendMessageW(parent_, kViewContextMenu, reinterpret_cast<WPARAM>(hwnd_),
                         MAKELPARAM(point.x, point.y));
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            if (pointOnScrollbar(point.x, point.y)) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            } else if (terminal_.mouseTracking()) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            }
            return TRUE;
        }
        break;
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_DESTROY:
        KillTimer(hwnd_, kBlinkTimer);
        blinkTimer_ = 0;
        session_.close();
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
