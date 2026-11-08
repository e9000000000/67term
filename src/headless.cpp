#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "headless.h"

#include "cli.h"
#include "input.h"
#include "menubar.h"
#include "pty.h"
#include "tabbar.h"
#include "terminal.h"
#include "theme.h"
#include "util.h"
#include "view.h"

namespace {

class Harness {
public:
    void check(bool ok, const std::wstring& name, const std::wstring& detail = L"") {
        if (ok) {
            ++passed;
            text += L"[ok] " + name + L"\r\n";
        } else {
            ++failed;
            text += L"[FAIL] " + name;
            if (!detail.empty()) {
                text += L" :: " + detail;
            }
            text += L"\r\n";
        }
    }

    void line(const std::wstring& value) { text += value + L"\r\n"; }

    void summary() {
        text += L"\r\nsummary passed=" + std::to_wstring(passed) + L" failed=" +
                std::to_wstring(failed) + L"\r\n";
    }

    std::wstring text;
    int passed = 0;
    int failed = 0;
};

std::wstring DefaultLogPath(const wchar_t* name) {
    wchar_t temp[MAX_PATH * 2]{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
    std::wstring path = length ? std::wstring(temp, length) : L".\\";
    path += name;
    return path;
}

void WriteLog(const std::wstring& path, const std::wstring& text) {
    const std::wstring target = path.empty() ? DefaultLogPath(L"67t-headless.log") : path;
    EnsureDirectory(DirectoryOf(target));
    std::wstring error;
    WriteTextFileAtomic(target, WideToUtf8(text), error);
}

std::wstring CellText(const vt::Terminal& terminal, int x, int y) {
    const vt::Cell& cell = terminal.viewCell(x, y);
    return std::wstring(cell.text);
}

std::wstring ScreenText(const vt::Terminal& terminal) {
    std::wstring text;
    for (int y = 0; y < terminal.rows(); ++y) {
        std::wstring row;
        for (int x = 0; x < terminal.columns(); ++x) {
            const vt::Cell& cell = terminal.viewCell(x, y);
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
        text.append(row);
        text.push_back(L'\n');
    }
    return text;
}

size_t ProcessRssBytes() {
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.WorkingSetSize;
    }
    return 0;
}

void TestCli(Harness& h) {
    LaunchOptions options = ParseOptions({L"--shell", L"cmd.exe", L"--cwd", ExeDirectory()});
    h.check(options.shell == L"cmd.exe", L"cli: --shell value");
    h.check(!options.cwd.empty(), L"cli: --cwd directory accepted");
    h.check(options.error.empty(), L"cli: no error for valid options");

    options = ParseOptions({L"--bench", L"500"});
    h.check(options.bench && options.benchLines == 500, L"cli: --bench line count");

    options = ParseOptions({L"--selftest"});
    h.check(options.selfTest, L"cli: --selftest flag");

    options = ParseOptions({L"--not-an-option"});
    h.check(!options.error.empty(), L"cli: unknown option reports error");
}

void TestUnicode(Harness& h) {
    h.check(vt::encodeUtf8(0x41) == "A", L"utf8: ASCII");
    h.check(vt::encodeUtf8(0x20AC) == "\xE2\x82\xAC", L"utf8: euro encode");
    size_t offset = 0;
    h.check(vt::decodeUtf8("\xE2\x82\xAC", offset) == 0x20AC && offset == 3,
            L"utf8: euro decode");
    offset = 0;
    h.check(vt::decodeUtf8("\xF0\x9F\x98\x80", offset) == 0x1F600 && offset == 4,
            L"utf8: emoji decode");
    h.check(vt::codePointWidth(0x41) == 1, L"width: ASCII");
    h.check(vt::codePointWidth(0x4E16) == 2, L"width: CJK wide");
    h.check(vt::codePointWidth(0x0301) == 0, L"width: combining");
    h.check(vt::codePointWidth(0x1F680) == 2, L"width: rocket emoji wide");
    h.check(vt::codePointWidth(0x2705) == 2, L"width: check mark emoji wide");
    h.check(vt::codePointWidth(0x2764) == 1, L"width: heart narrow without VS16");
}

void TestScreen(Harness& h) {
    vt::Terminal terminal(10, 4, 100);
    terminal.feed("hello");
    h.check(CellText(terminal, 0, 0) == L"h", L"screen: print cell");
    h.check(terminal.cursorX() == 5 && terminal.cursorY() == 0, L"screen: cursor after print");

    terminal.feed("\r\nworld");
    h.check(terminal.cursorY() == 1 && terminal.cursorX() == 5, L"screen: CRLF");

    terminal.feed("\x1b[2;3HX");
    h.check(CellText(terminal, 2, 1) == L"X", L"screen: CUP");
    h.check(terminal.cursorX() == 3, L"screen: cursor after CUP print");

    terminal.feed("\x1b[2J");
    h.check(CellText(terminal, 2, 1).empty(), L"screen: erase display");

    terminal.feed("\x1b[1;1H\x1b[31mR\x1b[1;4mB\x1b[38;2;10;20;30mT");
    const vt::Cell& red = terminal.viewCell(0, 0);
    h.check(red.fg.kind == vt::Color::Indexed && red.fg.index == 1, L"screen: SGR indexed fg");
    const vt::Cell& bold = terminal.viewCell(1, 0);
    h.check((bold.attrs & vt::AttrBold) != 0 && (bold.attrs & vt::AttrUnderline) != 0,
            L"screen: SGR bold+underline");

    const vt::Cell& rgb = terminal.viewCell(2, 0);
    h.check(rgb.fg.kind == vt::Color::Rgb && rgb.fg.r == 10 && rgb.fg.g == 20 &&
                rgb.fg.b == 30,
            L"screen: SGR truecolor");

    terminal.feed("\x1b[0m\x1b[44m\x1b[2J");
    h.check(terminal.viewCell(0, 0).bg.kind == vt::Color::Indexed &&
                terminal.viewCell(0, 0).bg.index == 4,
            L"screen: SGR background + ED uses pen");

    vt::Terminal wide(10, 2, 10);
    wide.feed("\xE4\xB8\x96X");
    h.check(wide.viewCell(0, 0).width == 2, L"screen: wide lead");
    h.check(wide.viewCell(1, 0).width == 0, L"screen: wide continuation");
    h.check(CellText(wide, 2, 0) == L"X", L"screen: wide advances two cells");

    vt::Terminal emoji(10, 2, 10);
    emoji.feed("\xF0\x9F\x9A\x80");  // U+1F680 rocket
    h.check(emoji.viewCell(0, 0).width == 2 && emoji.viewCell(1, 0).width == 0,
            L"screen: emoji is one wide cell");
    h.check(emoji.viewCell(0, 0).text[0] == 0xD83D && emoji.viewCell(0, 0).text[1] == 0xDE80,
            L"screen: emoji stored as a surrogate pair");
    h.check(emoji.cursorX() == 2, L"screen: emoji advances two cells");

    emoji.feed("\xEF\xB8\x8F");  // VS16
    const std::wstring lead = CellText(emoji, 0, 0);
    h.check(lead.size() == 3 && lead[0] == 0xD83D && lead[1] == 0xDE80 && lead[2] == 0xFE0F,
            L"screen: VS16 attaches to the emoji lead cell");
    h.check(CellText(emoji, 1, 0).empty() && emoji.viewCell(1, 0).width == 0,
            L"screen: VS16 leaves the continuation cell empty");
    h.check(emoji.cursorX() == 2, L"screen: VS16 keeps the wide advance");

    vt::Terminal heart(10, 2, 10);
    heart.feed("\xE2\x9D\xA4\xEF\xB8\x8F");  // U+2764 heart + VS16
    h.check(heart.viewCell(0, 0).width == 2 && heart.viewCell(1, 0).width == 0,
            L"screen: VS16 widens a text-style emoji");
    h.check(heart.cursorX() == 2, L"screen: widened emoji advances two cells");

    vt::Terminal tinted(10, 2, 10);
    tinted.feed("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD");  // thumb + skin tone
    h.check(tinted.viewCell(0, 0).width == 2 && tinted.viewCell(1, 0).width == 0 &&
                tinted.cursorX() == 2,
            L"screen: emoji modifier does not take a cell");

    vt::Terminal title(10, 2, 10);
    title.feed("\x1b]0;My Title\x07");
    h.check(title.title() == L"My Title", L"screen: OSC title");

    title.feed("\x1b]7;file:///C:/Temp\x07");
    h.check(title.workingDirectory() == L"C:\\Temp", L"screen: OSC 7 directory");

    std::string responses;
    title.setResponseCallback([&](std::string value) { responses += value; });
    title.feed("\x1b[c");
    h.check(responses.find("\x1b[?62") != std::string::npos, L"screen: DA response");
    responses.clear();
    title.feed("\x1b[2;3H\x1b[6n");
    h.check(responses == "\x1b[2;3R", L"screen: DSR cursor report");

    vt::Terminal modes(20, 5, 10);
    modes.feed("\x1b[?2004h");
    h.check(modes.bracketedPaste(), L"screen: bracketed paste on");
    modes.feed("\x1b[?2004l");
    h.check(!modes.bracketedPaste(), L"screen: bracketed paste off");
    modes.feed("\x1b[?1h");
    h.check(modes.applicationCursorKeys(), L"screen: application cursor keys");
    modes.feed("\x1b[?1002h\x1b[?1006h");
    h.check(modes.mouseTracking() && modes.mouseTrackingMode() == 1002 && modes.sgrMouse(),
            L"screen: mouse modes");
    modes.feed("\x1b[?9001h");
    h.check(modes.win32InputMode(), L"screen: win32 input mode on");
    modes.feed("\x1b[?9001l");
    h.check(!modes.win32InputMode(), L"screen: win32 input mode off");

    modes.feed("\x1b[?1049h");
    h.check(modes.altScreen(), L"screen: alt screen enter");
    modes.feed("\x1b[?1049l");
    h.check(!modes.altScreen(), L"screen: alt screen exit");

    vt::Terminal alt(10, 3, 10);
    alt.feed("main");
    alt.feed("\x1b[?1049h");
    alt.feed("alternate");
    h.check(CellText(alt, 0, 0) == L"a", L"screen: alt content");
    alt.feed("\x1b[?1049l");
    h.check(CellText(alt, 0, 0) == L"m", L"screen: main content restored");

    vt::Terminal scroll(10, 3, 100);
    scroll.feed("1\r\n2\r\n3\r\n4\r\n5");
    h.check(scroll.maxScrollOffset() >= 2, L"screen: scrollback grows");
    scroll.setScrollOffset(1);
    h.check(scroll.scrollOffset() == 1, L"screen: scroll offset set");
    scroll.setScrollOffset(0);
    h.check(scroll.scrollOffset() == 0, L"screen: scroll to bottom");

    vt::Terminal limited(10, 3, 2);
    limited.feed("1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8");
    h.check(limited.maxScrollOffset() <= 2, L"screen: scrollback limit");

    vt::Terminal resize(10, 3, 10);
    resize.feed("abcdef");
    resize.resize(4, 2);
    h.check(resize.columns() == 4 && resize.rows() == 2, L"screen: resize dimensions");
    h.check(resize.cursorX() <= 3 && resize.cursorY() <= 1, L"screen: resize clamps cursor");

    vt::Terminal split(10, 2, 0);
    split.feed("\x1b[3");
    split.feed("1mX");
    h.check(split.viewCell(0, 0).fg.kind == vt::Color::Indexed &&
                split.viewCell(0, 0).fg.index == 1,
            L"parser: split CSI across feeds");

    vt::Terminal utf(10, 2, 0);
    utf.feed("\xC3");
    utf.feed("\x28");
    h.check(CellText(utf, 0, 0) == L"\uFFFD", L"parser: invalid UTF-8 replacement");

    vt::Terminal decaln(5, 2, 0);
    decaln.feed("\x1b#8");
    h.check(CellText(decaln, 4, 1) == L"E", L"screen: DECALN");

    vt::Terminal save(10, 3, 0);
    save.feed("\x1b[3;4H\x1b" "7\x1b[1;1H\x1b" "8X");
    h.check(CellText(save, 3, 2) == L"X", L"screen: save/restore cursor");

    vt::Terminal repeat(10, 2, 0);
    repeat.feed("A\x1b[3b");
    h.check(CellText(repeat, 3, 0) == L"A", L"screen: REP");

    vt::Terminal tabs(30, 2, 0);
    tabs.feed("\tX");
    h.check(CellText(tabs, 8, 0) == L"X", L"screen: tab stop");

    vt::Terminal region(10, 6, 0);
    region.feed("\x1b[2;4r");
    region.feed("\x1b[4;1H\x1bM");
    h.check(region.scrollTop() == 1 && region.scrollBottom() == 3, L"screen: scroll region set");
}

void TestInput(Harness& h) {
    input::KeyEvent event;
    event.key = VK_UP;
    std::string sequence;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1b[A", L"input: up");
    event.applicationCursor = true;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1bOA", L"input: app cursor up");
    event.applicationCursor = false;
    event.shift = true;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1b[1;2A", L"input: shift up");
    event.shift = false;
    event.ctrl = true;
    event.key = VK_RIGHT;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1b[1;5C", L"input: ctrl right");
    event.ctrl = false;
    event.key = VK_F5;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1b[15~", L"input: F5");
    event.ctrl = true;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1b[15;5~", L"input: ctrl F5");
    event.ctrl = false;
    event.key = VK_TAB;
    event.shift = true;
    h.check(input::EncodeKey(event, sequence) && sequence == "\x1b[Z", L"input: shift tab");

    h.check(input::EncodeMouse(0, 4, 2, false, false, true, 0) == "\x1b[<0;5;3M",
            L"input: SGR mouse press");
    h.check(input::EncodeMouse(0, 4, 2, true, false, true, 0) == "\x1b[<0;5;3m",
            L"input: SGR mouse release");
    h.check(input::EncodeMouse(0, 4, 2, false, true, true, 0) == "\x1b[<32;5;3M",
            L"input: SGR mouse motion");
    const std::string legacy = input::EncodeMouse(0, 0, 0, true, false, false, 0);
    h.check(legacy.size() == 6 && legacy[3] == static_cast<char>(32 + 3), L"input: legacy release");

    h.check(input::EncodeWin32Key(89, 21, 25, true, 8, 1) == "\x1b[89;21;25;1;8;1_",
            L"input: win32 key record");
    h.check(input::EncodeWin32Key(8, 14, 0, false, 8, 1) == "\x1b[8;14;0;0;8;1_",
            L"input: win32 key release");

    h.check(input::NormalizePaste(L"a\r\nb\rc\nd") == L"a\rb\rc\nd", L"input: paste newlines");
}

void TestPty(Harness& h) {
    h.check(PtySession::available(), L"pty: ConPTY available");
    if (!PtySession::available()) {
        return;
    }

    const auto runCommand = [&](const std::wstring& command, std::string& output,
                                std::wstring& error) {
        PtySession session;
        if (!session.start(command, ExeDirectory(), 60, 20, nullptr, error)) {
            return false;
        }
        const ULONGLONG deadline = GetTickCount64() + 12000;
        while (GetTickCount64() < deadline) {
            std::string data;
            if (session.drain(data)) {
                output += data;
            }
            if (session.exited()) {
                std::string tail;
                if (session.drain(tail)) {
                    output += tail;
                }
                break;
            }
            Sleep(15);
        }
        const bool exited = session.exited();
        session.close();
        return exited;
    };

    std::string output;
    std::wstring error;
    const bool exited = runCommand(L"cmd.exe /d /c \"echo conpty-ok\"", output, error);
    h.check(exited, L"pty: child exits", error);
    h.check(output.find("conpty-ok") != std::string::npos, L"pty: output received",
            Utf8ToWide(output.substr(0, 160)));

    output.clear();
    error.clear();
    runCommand(L"cmd.exe /d /c \"echo TERM=%TERM%\"", output, error);
    h.check(output.find("xterm-256color") != std::string::npos, L"pty: TERM environment",
            Utf8ToWide(output.substr(0, 160)));

    PtySession resize;
    if (resize.start(L"cmd.exe /d /c \"ping -n 2 127.0.0.1 >nul & echo resized-ok\"", ExeDirectory(),
                     50, 15, nullptr, error)) {
        resize.resize(70, 25);
        h.check(resize.columns() == 70 && resize.rows() == 25, L"pty: resize bookkeeping");
        output.clear();
        const ULONGLONG deadline = GetTickCount64() + 12000;
        while (GetTickCount64() < deadline && !resize.exited()) {
            std::string data;
            if (resize.drain(data)) {
                output += data;
            }
            Sleep(15);
        }
        std::string tail;
        if (resize.drain(tail)) {
            output += tail;
        }
        h.check(output.find("resized-ok") != std::string::npos, L"pty: output after resize");
        resize.close();
    } else {
        h.check(false, L"pty: resize session start", error);
    }

    PtySession cwdSession;
    error.clear();
    if (cwdSession.start(L"cmd.exe /d /k", ExeDirectory(), 60, 20, nullptr, error)) {
        std::wstring directory;
        const ULONGLONG cwdDeadline = GetTickCount64() + 3000;
        while (GetTickCount64() < cwdDeadline && directory.empty()) {
            directory = ProcessCurrentDirectory(cwdSession.processId());
            if (directory.empty()) {
                Sleep(20);
            }
        }
        h.check(!directory.empty() && PathsEqual(directory, ExeDirectory()),
                L"pty: process current directory", directory);
        cwdSession.close();
    } else {
        h.check(false, L"pty: cwd session start", error);
    }
}

void TestWin32Input(Harness& h) {
    if (!PtySession::available()) {
        return;
    }
    PtySession session;
    std::wstring error;
    if (!session.start(L"powershell.exe -NoLogo -NoProfile", ExeDirectory(), 100, 30, nullptr,
                       error)) {
        h.check(false, L"win32 input: session start", error);
        return;
    }

    vt::Terminal terminal(100, 30, 1000);
    const auto pump = [&]() {
        std::string data;
        if (session.drain(data)) {
            terminal.feed(data);
        }
    };

    const ULONGLONG readyDeadline = GetTickCount64() + 15000;
    while (GetTickCount64() < readyDeadline) {
        pump();
        if (terminal.win32InputMode() && ScreenText(terminal).find(L"PS ") != std::wstring::npos) {
            break;
        }
        Sleep(20);
    }
    h.check(terminal.win32InputMode(), L"win32 input: ConPTY enabled the mode");

    const auto key = [&](int vk, int ch, bool ctrl) {
        const int scan = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
        const int state = ctrl ? 0x0008 : 0;
        session.write(input::EncodeWin32Key(vk, scan, ch, true, state, 1));
        session.write(input::EncodeWin32Key(vk, scan, 0, false, state, 1));
        Sleep(25);
    };

    for (const wchar_t ch : std::wstring(L"echo abc def")) {
        if (ch == L' ') {
            key(VK_SPACE, L' ', false);
        } else if (ch >= L'a' && ch <= L'z') {
            key(ch - L'a' + L'A', ch, false);
        } else if (ch >= L'0' && ch <= L'9') {
            key(ch, ch, false);
        }
    }
    key(VK_BACK, 8, true);
    key(VK_RETURN, 13, false);

    const ULONGLONG resultDeadline = GetTickCount64() + 8000;
    std::wstring screen;
    while (GetTickCount64() < resultDeadline) {
        pump();
        screen = ScreenText(terminal);
        if (screen.find(L"echo abc") != std::wstring::npos &&
            screen.find(L"abc") != std::wstring::npos) {
            break;
        }
        Sleep(30);
    }
    screen = ScreenText(terminal);
    h.check(screen.find(L"echo abc") != std::wstring::npos,
            L"win32 input: Ctrl+Backspace edited the command line",
            screen.substr(0, 240));
    h.check(screen.find(L"def") == std::wstring::npos,
            L"win32 input: deleted word is gone");

    // Without a WM_CHAR character, the key-up flush still carries VK_BACK + Ctrl.
    for (const wchar_t ch : std::wstring(L"echo one two")) {
        if (ch == L' ') {
            key(VK_SPACE, L' ', false);
        } else if (ch >= L'a' && ch <= L'z') {
            key(ch - L'a' + L'A', ch, false);
        }
    }
    {
        const int scan = static_cast<int>(MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC));
        session.write(input::EncodeWin32Key(VK_BACK, scan, 0, true, 0x0008, 1));
        session.write(input::EncodeWin32Key(VK_BACK, scan, 0, false, 0x0008, 1));
    }
    key(VK_RETURN, 13, false);

    const ULONGLONG secondDeadline = GetTickCount64() + 8000;
    while (GetTickCount64() < secondDeadline) {
        pump();
        screen = ScreenText(terminal);
        if (screen.find(L"echo one") != std::wstring::npos &&
            screen.find(L"one") != std::wstring::npos) {
            break;
        }
        Sleep(30);
    }
    screen = ScreenText(terminal);
    h.check(screen.find(L"echo one") != std::wstring::npos,
            L"win32 input: Ctrl+Backspace works with an empty Unicode field",
            screen.substr(0, 240));
    h.check(screen.find(L"two") == std::wstring::npos, L"win32 input: second word is gone");
    session.close();
}

LRESULT CALLBACK HiddenHostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void TestStartupComponents(Harness& h) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HiddenHostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"67tHeadlessHost";
    RegisterClassExW(&wc);
    HWND host = CreateWindowExW(0, L"67tHeadlessHost", L"", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1200,
                                780, nullptr, nullptr, wc.hInstance, nullptr);
    h.check(host != nullptr, L"startup: hidden host window");
    if (host) {
        const ULONGLONG start = GetTickCount64();
        MenuBar menu;
        TabBar tabs;
        TerminalView view;
        const bool chrome = menu.create(host, wc.hInstance) && tabs.create(host, wc.hInstance);
        const bool viewCreated = view.create(host, wc.hInstance);
        h.check(chrome, L"startup: chrome creates");
        h.check(viewCreated, L"startup: terminal view creates");
        menu.layout(0, 0, 1200, 28);
        tabs.layout(0, 0, 1200, 28);
        view.layout(0, 28, 1200, 724);
        const ULONGLONG elapsed = GetTickCount64() - start;
        h.check(elapsed < 500, L"startup: chrome construction under 500 ms",
                std::to_wstring(elapsed) + L" ms");
        h.line(L"startup ms=" + std::to_wstring(elapsed));
        DestroyWindow(host);
    }
}

}  // namespace

int RunSelfTest(const std::wstring& logPath) {
    Harness harness;
    harness.line(L"67t self-test");
    harness.line(L"ConPTY: " + std::wstring(PtySession::available() ? L"yes" : L"no"));

    TestCli(harness);
    TestUnicode(harness);
    TestScreen(harness);
    TestInput(harness);
    TestPty(harness);
    TestWin32Input(harness);
    TestStartupComponents(harness);

    harness.summary();
    WriteLog(logPath, harness.text);
    OutputDebugStringW(harness.text.c_str());
    return harness.failed == 0 ? 0 : 1;
}

int RunBench(int lines, const std::wstring& logPath) {
    Harness output;
    lines = std::max(1, lines);
    vt::Terminal terminal(120, 40, 10000);

    std::string data;
    data.reserve(static_cast<size_t>(lines) * 96);
    for (int i = 0; i < lines; ++i) {
        data += "\x1b[38;5;" + std::to_string(16 + (i % 200)) + "m" + std::to_string(i) +
                " \x1b[1mbold\x1b[0m \xE4\xB8\x96\xE7\x95\x8C caf\xC3\xA9\r\n";
    }

    const auto feedStart = std::chrono::steady_clock::now();
    terminal.feed(data);
    const auto feedEnd = std::chrono::steady_clock::now();
    const double feedMs = std::chrono::duration<double, std::milli>(feedEnd - feedStart).count();

    const auto resizeStart = std::chrono::steady_clock::now();
    terminal.resize(100, 30);
    terminal.resize(140, 45);
    terminal.resize(120, 40);
    const auto resizeEnd = std::chrono::steady_clock::now();
    const double resizeMs = std::chrono::duration<double, std::milli>(resizeEnd - resizeStart).count();

    output.line(L"67t bench");
    output.line(L"lines=" + std::to_wstring(lines));
    output.line(L"bytes=" + std::to_wstring(data.size()));
    output.line(L"feed_ms=" + std::to_wstring(feedMs));
    output.line(L"ms_per_1k_lines=" + std::to_wstring(feedMs * 1000.0 / lines));
    output.line(L"resize_ms=" + std::to_wstring(resizeMs));
    output.line(L"scrollback=" + std::to_wstring(terminal.maxScrollOffset()));
    output.line(L"rss_bytes=" + std::to_wstring(ProcessRssBytes()));
    output.line(L"summary bench lines=" + std::to_wstring(lines));
    WriteLog(logPath, output.text);
    OutputDebugStringW(output.text.c_str());
    return 0;
}

int RunStartupBench(const std::wstring& logPath) {
    Harness output;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HiddenHostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"67tStartupHost";
    RegisterClassExW(&wc);

    const ULONGLONG start = GetTickCount64();
    HWND host = CreateWindowExW(0, L"67tStartupHost", L"", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1200,
                                780, nullptr, nullptr, wc.hInstance, nullptr);
    MenuBar menu;
    TabBar tabs;
    TerminalView view;
    const bool ok = host && menu.create(host, wc.hInstance) && tabs.create(host, wc.hInstance) &&
                    view.create(host, wc.hInstance);
    menu.layout(0, 0, 1200, 28);
    tabs.layout(0, 0, 1200, 28);
    view.layout(0, 28, 1200, 724);
    const ULONGLONG elapsed = GetTickCount64() - start;

    output.line(L"67t startup bench");
    output.line(L"ok=" + std::wstring(ok ? L"1" : L"0"));
    output.line(L"startup_ms=" + std::to_wstring(elapsed));
    output.line(L"rss_bytes=" + std::to_wstring(ProcessRssBytes()));
    output.line(L"summary startup");
    WriteLog(logPath, output.text);
    if (host) {
        DestroyWindow(host);
    }
    return ok ? 0 : 1;
}
