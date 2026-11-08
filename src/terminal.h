#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

// ---------------------------------------------------------------- colors

struct Color {
    enum Kind : uint8_t { Default = 0, Indexed = 1, Rgb = 2 };
    uint8_t kind = Default;
    uint8_t index = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    static Color DefaultColor() { return Color{}; }
    static Color IndexedColor(uint8_t value) {
        Color c;
        c.kind = Indexed;
        c.index = value;
        return c;
    }
    static Color RgbColor(uint8_t red, uint8_t green, uint8_t blue) {
        Color c;
        c.kind = Rgb;
        c.r = red;
        c.g = green;
        c.b = blue;
        return c;
    }
    bool operator==(const Color&) const = default;
};

// ---------------------------------------------------------------- cells

enum Attr : uint16_t {
    AttrBold = 1u << 0,
    AttrDim = 1u << 1,
    AttrItalic = 1u << 2,
    AttrUnderline = 1u << 3,
    AttrBlink = 1u << 4,
    AttrInverse = 1u << 5,
    AttrInvisible = 1u << 6,
    AttrStrike = 1u << 7,
};

struct Cell {
    wchar_t text[4]{};  // base character (or surrogate pair) plus combining marks
    uint8_t width = 1;  // 0 = wide continuation, 1 = narrow, 2 = wide lead
    uint16_t attrs = 0;
    Color fg{};
    Color bg{};

    bool empty() const { return text[0] == 0 && width == 1; }
};

struct Line {
    std::vector<Cell> cells;
};

enum class CursorShape : uint8_t { Block, Underline, Bar };

// ---------------------------------------------------------------- terminal

class Terminal {
public:
    Terminal(int columns, int rows, int scrollbackLimit = 10000);

    // Raw PTY output (UTF-8, possibly split at arbitrary byte boundaries).
    void feed(std::string_view bytes);

    void reset(bool hard);
    void resize(int columns, int rows);

    int columns() const { return columns_; }
    int rows() const { return rows_; }
    int scrollbackLimit() const { return scrollbackLimit_; }
    void setScrollbackLimit(int lines);

    // Visible viewport (row 0 = top of the window). Includes scrollback when the
    // user has scrolled up.
    const Cell& viewCell(int x, int y) const;
    const Line* viewLine(int y) const;

    int scrollOffset() const { return scrollOffset_; }
    int maxScrollOffset() const;
    void setScrollOffset(int offset);
    void scrollBy(int lines);
    void clearScrollback();

    int cursorX() const { return cursorX_; }
    int cursorY() const { return cursorY_; }
    bool cursorVisible() const { return cursorVisible_; }
    bool cursorBlink() const { return cursorBlink_; }
    CursorShape cursorShape() const { return cursorShape_; }
    bool reverseVideo() const { return reverseVideo_; }

    bool altScreen() const { return altActive_; }
    bool applicationCursorKeys() const { return applicationCursorKeys_; }
    bool applicationKeypad() const { return applicationKeypad_; }
    bool bracketedPaste() const { return bracketedPaste_; }
    bool focusReporting() const { return focusReporting_; }
    bool mouseTracking() const { return mouseTracking_ != 0; }
    int mouseTrackingMode() const { return mouseTracking_; }
    bool sgrMouse() const { return sgrMouse_; }
    bool newlineMode() const { return newlineMode_; }
    bool insertMode() const { return insertMode_; }
    bool autowrap() const { return autowrap_; }
    bool win32InputMode() const { return win32InputMode_; }

    const std::wstring& title() const { return title_; }
    const std::wstring& workingDirectory() const { return workingDirectory_; }

    int scrollTop() const { return scrollTop_; }
    int scrollBottom() const { return scrollBottom_; }

    // Cell geometry used for pixel-size reports (CSI 14 t / 16 t).
    void setPixelSize(int width, int height);
    int pixelWidth() const { return pixelWidth_; }
    int pixelHeight() const { return pixelHeight_; }

    // Outbound VT bytes (device attributes, cursor reports, mode reports).
    void setResponseCallback(std::function<void(std::string)> callback) {
        response_ = std::move(callback);
    }
    void setTitleCallback(std::function<void(const std::wstring&)> callback) {
        titleCallback_ = std::move(callback);
    }
    void setBellCallback(std::function<void()> callback) { bellCallback_ = std::move(callback); }
    void setClipboardCallback(std::function<void(const std::wstring&)> callback) {
        clipboardCallback_ = std::move(callback);
    }
    void setDirectoryCallback(std::function<void(const std::wstring&)> callback) {
        directoryCallback_ = std::move(callback);
    }

    // Debug/self-test accessors.
    bool hasSelectionAttributes() const { return false; }
    const std::string& lastResponse() const { return lastResponse_; }

private:
    enum class State : uint8_t {
        Ground,
        Escape,
        EscapeIntermediate,
        Charset,
        Hash,
        Csi,
        Osc,
        OscEscape,
        Dcs,
        DcsEscape,
        IgnoreString,
    };

    void printCodePoint(uint32_t cp);
    void printAscii(uint8_t ch);
    void execute(uint8_t ch);
    void dispatchEscape(uint8_t final);
    void dispatchCsi(std::string_view params, std::string_view intermediates, uint8_t final);
    void dispatchOsc(std::string_view payload);
    void dispatchDcs(std::string_view payload);
    void respond(std::string text);

    void lineFeed();
    void reverseIndex();
    void carriageReturn();
    void tabForward(int count = 1);
    void tabBackward(int count = 1);
    void nextTabStop();
    void previousTabStop();

    void scrollUp(int top, int bottom, int count);
    void scrollDown(int top, int bottom, int count);
    void pushScrollback(Line line);
    void clearLine(int y);
    void eraseCells(int y, int x, int count);
    void putCell(int x, int y, const Cell& cell);
    void clearWideNeighbor(int x, int y);
    Cell blankCell() const;
    Cell eraseCell() const;

    std::vector<Line>& grid();
    const std::vector<Line>& grid() const;
    Line& lineAt(int y);
    const Line& lineAt(int y) const;
    Line& combinedLine(int absoluteRow);
    const Line* combinedLinePtr(int absoluteRow) const;

    void moveCursor(int x, int y, bool clampToOrigin = true);
    void cursorHome();
    void saveCursor();
    void restoreCursor();
    void resetPen();
    void resetModes();
    void setAltScreen(bool enable, bool clear, bool saveCursorFirst);
    void setMode(int mode, bool enabled, bool privateMode);
    void applySgr(std::string_view params);
    void setCursorStyleFromParams(std::string_view params);
    void reportMode(int mode, bool privateMode);
    void deviceAttributes(bool secondary);
    void windowOp(int op, int a, int b);
    void softReset();

    void onOscTitle(std::wstring title);
    void onOscDirectory(std::string_view uri);
    void onOscClipboard(std::string_view payload);

    // Grids.
    std::vector<Line> main_;
    std::vector<Line> alt_;
    std::deque<Line> scrollback_;

    int columns_ = 80;
    int rows_ = 24;
    int scrollbackLimit_ = 10000;
    int scrollOffset_ = 0;
    bool altActive_ = false;

    int cursorX_ = 0;
    int cursorY_ = 0;
    bool cursorVisible_ = true;
    bool cursorBlink_ = true;
    CursorShape cursorShape_ = CursorShape::Block;
    bool wrapPending_ = false;

    uint16_t penAttrs_ = 0;
    Color penFg_{};
    Color penBg_{};
    uint32_t lastPrinted_ = 0;

    int scrollTop_ = 0;
    int scrollBottom_ = 23;
    bool autowrap_ = true;
    bool insertMode_ = false;
    bool originMode_ = false;
    bool newlineMode_ = false;
    bool reverseVideo_ = false;
    bool applicationCursorKeys_ = false;
    bool applicationKeypad_ = false;
    bool bracketedPaste_ = false;
    bool focusReporting_ = false;
    bool win32InputMode_ = false;
    int mouseTracking_ = 0;  // 0, 1000, 1002, 1003
    bool sgrMouse_ = false;

    std::vector<int> tabStops_;

    struct SavedCursor {
        int x = 0;
        int y = 0;
        uint16_t attrs = 0;
        Color fg{};
        Color bg{};
        bool origin = false;
        bool valid = false;
    };
    SavedCursor savedCursor_;

    std::wstring title_;
    std::wstring workingDirectory_;
    std::wstring titleStack_;

    int pixelWidth_ = 0;
    int pixelHeight_ = 0;

    // Parser state.
    State state_ = State::Ground;
    std::string csiParams_;
    std::string csiIntermediates_;
    std::string osc_;
    std::string dcs_;
    std::string lastResponse_;
    uint8_t escIntermediate_ = 0;

    uint32_t utf8CodePoint_ = 0;
    uint32_t utf8Minimum_ = 0;
    int utf8Remaining_ = 0;

    std::function<void(std::string)> response_;
    std::function<void(const std::wstring&)> titleCallback_;
    std::function<void()> bellCallback_;
    std::function<void(const std::wstring&)> clipboardCallback_;
    std::function<void(const std::wstring&)> directoryCallback_;
};

// Helpers shared with the renderer and tests.
int codePointWidth(uint32_t codepoint);
void appendUtf16(wchar_t (&dest)[4], uint32_t codepoint);
std::string encodeUtf8(uint32_t codepoint);
uint32_t decodeUtf8(std::string_view text, size_t& offset);

}  // namespace vt
