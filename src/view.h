#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "pty.h"
#include "terminal.h"

// Viewer -> app notifications (wParam/lParam = view HWND).
constexpr UINT kViewTitleChanged = WM_APP + 0x221;
constexpr UINT kViewStateChanged = WM_APP + 0x222;
constexpr UINT kViewExited = WM_APP + 0x223;
constexpr UINT kViewBell = WM_APP + 0x224;
constexpr UINT kViewContextMenu = WM_APP + 0x225;

class TerminalView {
public:
    TerminalView();
    ~TerminalView();

    TerminalView(const TerminalView&) = delete;
    TerminalView& operator=(const TerminalView&) = delete;

    bool create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    void layout(int x, int y, int width, int height);
    void show(bool visible);
    void focus();
    void invalidate();

    void setAppearance(const std::wstring& fontFamily, int fontPointSize, bool cursorBlink,
                       int scrollback);
    void applyDpi(UINT dpi);

    bool start(const std::wstring& commandLine, const std::wstring& workingDirectory,
               const std::wstring& titleHint);
    void restart();
    void closeSession();

    int columns() const { return columns_; }
    int rows() const { return rows_; }
    int cellWidth() const { return cellWidth_; }
    int cellHeight() const { return cellHeight_; }
    int cursorRow() const { return terminal_.cursorY(); }
    int cursorColumn() const { return terminal_.cursorX(); }
    int scrollOffset() const { return terminal_.scrollOffset(); }
    int scrollbackLines() const { return static_cast<int>(terminal_.maxScrollOffset()); }
    int scrollbackLimit() const { return terminal_.scrollbackLimit(); }

    const std::wstring& title() const;
    const std::wstring& workingDirectory() const { return terminal_.workingDirectory(); }
    const std::wstring& shellCommand() const { return shellCommand_; }
    bool promptReady() const { return !starting_ && !exited_ && !startFailed_; }
    bool exited() const { return exited_; }
    bool startFailed() const { return startFailed_; }
    bool hasSelection() const;
    bool sessionRunning() const { return session_.running(); }
    DWORD processId() const { return session_.processId(); }

    std::wstring selectedText() const;
    void copySelection();
    void pasteClipboard();
    void pasteText(const std::wstring& text);
    void selectAll();
    void clearSelection();
    void scrollToBottom();
    void scrollLines(int lines);
    void clearScrollback();
    void writeInput(std::string_view bytes);

    void setSearchState(bool active) { searchActive_ = active; }
    void setScrollbackLimit(int lines);

    std::function<void()> onTitleChanged;
    std::function<void()> onStateChanged;
    std::function<void()> onExited;
    std::function<void()> onBell;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureFonts();
    void measure();
    void applySize();
    void feedFromSession();
    void onSessionExit();
    void paint(HDC target);
    void paintGrid(HDC dc, const RECT& client);
    void paintCursor(HDC dc);
    void paintScrollbar(HDC dc, const RECT& client);

    COLORREF resolveColor(const vt::Color& color, bool foreground) const;
    COLORREF cellForeground(const vt::Cell& cell) const;
    COLORREF cellBackground(const vt::Cell& cell) const;
    bool cellSelected(int x, int y) const;

    void notifyParent(UINT message);
    void sendText(std::string_view bytes);
    void sendUtf16(std::wstring_view text);
    void sendKey(WPARAM key, bool ctrl, bool alt, bool shift, bool extended);
    void sendMouse(WORD button, int x, int y, bool press, bool wheel, int wheelDelta);
    std::string encodeMouse(int button, int x, int y, bool release, bool motion) const;

    void beginSelection(int x, int y);
    void updateSelection(int x, int y);
    void endSelection();
    void selectWordAt(int x, int y);
    void selectLineAt(int x, int y);
    void focusCellFromPoint(int px, int py, int& x, int& y) const;
    bool pointOnScrollbar(int x, int y) const;
    void scrollToPoint(int y);
    void scrollWithDelta(int lines);

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HINSTANCE instance_ = nullptr;

    std::wstring fontFamily_ = L"Cascadia Mono";
    int fontPointSize_ = 16;
    bool blinkSetting_ = true;
    int scrollbackLimit_ = 10000;

    HFONT regularFont_ = nullptr;
    HFONT boldFont_ = nullptr;
    HFONT italicFont_ = nullptr;
    HFONT boldItalicFont_ = nullptr;
    UINT dpi_ = 96;
    int cellWidth_ = 8;
    int cellHeight_ = 16;
    int ascent_ = 12;
    int textHeight_ = 14;
    int padding_ = 8;
    int scrollbarWidth_ = 6;

    int columns_ = 80;
    int rows_ = 24;

    vt::Terminal terminal_{80, 24, 10000};
    PtySession session_;
    std::wstring shellCommand_;
    std::wstring workingDirectory_;
    std::wstring titleHint_;
    bool starting_ = false;
    bool startFailed_ = false;
    bool exited_ = false;

    bool focused_ = false;
    bool cursorOn_ = true;
    bool blinkPhase_ = true;
    UINT_PTR blinkTimer_ = 0;
    ULONGLONG bellUntil_ = 0;

    bool selecting_ = false;
    bool haveSelection_ = false;
    int selectionAnchorX_ = 0;
    int selectionAnchorY_ = 0;
    int selectionStartX_ = 0;
    int selectionStartY_ = 0;
    int selectionEndX_ = 0;
    int selectionEndY_ = 0;

    bool scrollbarDragging_ = false;
    int scrollbarDragOffset_ = 0;

    bool mouseButtonDown_ = false;
    int mouseButton_ = 0;
    int lastMouseCellX_ = -1;
    int lastMouseCellY_ = -1;
    ULONGLONG lastClickTick_ = 0;
    int clickCount_ = 0;
    int lastClickX_ = -1;
    int lastClickY_ = -1;

    wchar_t pendingHighSurrogate_ = 0;
    bool suppressNextChar_ = false;
    bool searchActive_ = false;
};
