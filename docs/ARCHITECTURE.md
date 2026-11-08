# 67t — Architecture

## Process model

One process owns one window, any number of tabs, and one ConPTY session per tab.

```
67t.exe
├─ main window (custom chrome)
│  ├─ title/tab strip: burger menu + tabs + fixed new-tab button
│  ├─ owner-drawn popup menus (hidden MenuBar host)
│  ├─ one terminal child HWND per tab (only active visible)
│  │  ├─ Terminal (parser + screen + scrollback)
│  │  └─ PtySession (HPCON, child process, reader thread)
└─ settings.ini / window placement
```

Closing a tab destroys its view and closes its pseudo console, which ends the
shell. There is no daemon and no network listener. The only cross-process
handoffs are single-instance tab forwarding and the optional Windows
default-terminal COM delegation described below.

## Single instance

The first process in a session creates a hidden top-level receiver window
(class `67tInstanceHost`, `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`). A later launch
finds it with `FindWindowW` and sends `WM_COPYDATA` containing the requested
working directory and optional shell command. The receiver opens a new tab and
brings the window to the foreground. Startup is serialized with the named mutex
`Local\67t.SingleInstance.7A1B`, so two simultaneous launches cannot both create
a receiver.

The receiver must be a normal hidden window, not `HWND_MESSAGE`: message-only
windows are not visible to `FindWindowW`. Delegated default-terminal launches
(`-Embedding`) skip forwarding because they already carry a console handoff, but
after creating their window they register as the primary instance if none
exists, so a later ordinary launch joins them. `--new-window` opts out of
forwarding for ordinary launches.

## Why ConPTY

`CreatePseudoConsole` is the only supported way to host a real Windows console
program in a GUI. The shell sees a genuine console: it gets a window size, VT
input, and VT output. We never scrape redirected `cmd` output.

ConPTY is available from Windows 10 1809. The app loads its exports from
`kernel32.dll` at startup so an older OS fails with a clear error instead of a
load-time failure.

## PTY session

`PtySession::start`:

1. `CreatePipe` for input and output with inheritable ConPTY-side ends only.
2. `CreatePseudoConsole({cols, rows}, ptyInputRead, ptyOutputWrite, 0, &hpc)`.
3. Close the two ConPTY-side handles in the parent.
4. Build a UTF-8/UTF-16 environment block that includes `TERM=xterm-256color`
   and `COLORTERM=truecolor`.
5. `CreateProcessW` the shell with
   `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` and
   `EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT`.
6. Start a reader thread on the output pipe.

The reader thread reads 16 KiB at a time into a mutex-protected backlog and calls
`PostMessage` once when the backlog transitions from empty to non-empty. The UI
thread drains the backlog on `WM_APP_PTY_DATA`, feeds the parser, and re-posts if
more remains.

Backlog is bounded (8 MiB). If a shell floods output faster than the UI can draw,
the reader blocks on the pipe once the cap is reached, which applies natural
backpressure to the child.

`write` queues UTF-8 bytes for a writer thread, which `WriteFile`s them to the
input pipe. Keeping pipe writes off the UI thread means a console client that
stops reading its input can never block painting or input handling. The queue is
bounded (4 MiB). `resize` calls `ResizePseudoConsole` only when the grid size
actually changes.

Shutdown order: `TerminateProcess` if the child is still alive, close the
pseudo console, close pipes, join the reader, close handles. `ClosePseudoConsole`
is never called from the reader thread.

The tab's directory comes from OSC 7 when the shell reports it; otherwise
`ProcessCurrentDirectory` reads `RTL_USER_PROCESS_PARAMETERS.CurrentDirectory`
through the shell's PEB, so New Tab inherits the directory without shell
integration.

## Default terminal (console delegation)

Windows can hand a new console session to a registered console host and then to
a registered terminal. 67t participates in the second half of that chain.

```
client (cmd.exe)
└─ conhost.exe          inbox console, stays alive for signalling
   ├─ OpenConsole.exe   delegated console host, -Embedding COM server
   │  └─ 67t.exe        terminal, -Embedding COM server
   └─ ConPTY pipes ─────► attached tab
```

Setting 67t as the default writes `HKCU\Console\%%Startup`:

- `DelegationConsole` = Windows Terminal's stable OpenConsole CLSID
  (`{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}`)
- `DelegationTerminal` = 67t's terminal CLSID (`{AE70E244-…-B17033538EDB}`)

`defterm.cpp` finds the installed Windows Terminal package, copies its
`OpenConsoleProxy.dll` to `%APPDATA%\67t`, and registers:

- `HKCU\Software\Classes\CLSID\{67t terminal}` → `LocalServer32` =
  `"…\67t.exe" -Embedding`
- `HKCU\Software\Classes\CLSID\{OpenConsole proxy}` → `InprocServer32` pointing
  at the copied DLL
- `HKCU\Software\Classes\Interface\{ITerminalHandoff3}` → `ProxyStubClsid32`

The previous pair is saved under `HKCU\Software\67t\DefaultTerminal` and restored
by the menu or `--restore-default-terminal`.

Every 67t window registers an `IClassFactory` for the terminal CLSID with
`REGCLS_MULTIPLEUSE` and keeps it registered for the window's lifetime, so COM
hands later delegated consoles to that process instead of launching another
`67t.exe`. A delegated console therefore opens as a tab in the running window.
When no window exists, COM launches `67t.exe -Embedding`; `handoff::RunServer`
registers the class, pumps until the first handoff arrives, and then leaves the
registration active while the new window runs.

OpenConsole calls `ITerminalHandoff3::EstablishPtyHandoff`, which 67t handles by:

1. Duplicating the signal, reference, server (OpenConsole) and client process
   handles.
2. Creating a named duplex pipe with `FILE_FLAG_OVERLAPPED` on both ends, keeping
   the server end.
3. Returning two duplicated client ends as `in` and `out`, which COM marshals to
   OpenConsole.
4. Handing the pipe and process handles to a new tab via `PtySession::startAttached`.

The overlapped flag is required: ConPTY does overlapped I/O on its ends, and a
synchronous pipe delivers the initial handshake and then stalls.

Attached sessions differ from spawned ones:

- No `CreatePseudoConsole` and no `CreateProcess`; the client already exists.
- Reads and writes use `OVERLAPPED` events; the pipe is overlapped.
- Resize sends the signal-pipe packet `{8, columns, rows}` as three `unsigned
  short`s (`PTY_SIGNAL_RESIZE_WINDOW`).
- Closing the tab closes the pipe and terminates the client; the waiter thread
  tracks the client process handle.

## Terminal engine

`terminal.h` has no Win32 dependency, which makes it testable headless.

```cpp
struct Cell {
    wchar_t text[4];   // base char or surrogate pair + combining marks
    uint8_t width;     // 0 continuation, 1 narrow, 2 wide
    uint16_t attrs;    // bold, dim, italic, underline, blink, inverse, ...
    Color fg, bg;      // default / indexed / RGB
};

struct Line { std::vector<Cell> cells; };
```

State:

- main screen and alternate screen grids
- scrollback `std::deque<Line>` (main screen only)
- cursor position, deferred-wrap flag, cursor style/visibility
- saved cursor (DECSC)
- current pen: attrs + fg/bg
- scroll region (DECSTBM), origin mode, autowrap, insert mode
- tab stops
- private modes: cursor keys, keypad, bracketed paste, focus, mouse (1000/1002/
  1003), SGR mouse (1006), alt screen (47/1047/1049), reverse video (5),
  Win32 input mode (9001)
- title (OSC 0/1/2), working directory (OSC 7), clipboard callback (OSC 52)
- response callback for DA, DSR, window-size reports, DECRQM

Parser is an incremental state machine:

```
Ground --ESC--> Escape --[--> Csi --> final dispatch
                  |--]--> Osc --> BEL/ST
                  |--P--> Dcs (ignored to ST)
                  |--( ) * +--> Charset (consumed)
```

CSI parameters and intermediates are collected as raw ASCII and parsed at final
dispatch, so split reads and unusual orders are handled without allocating per
byte. OSC strings are capped at 8 KiB. UTF-8 decoding keeps partial-sequence
state across `feed()` calls; malformed bytes become U+FFFD.

Cell semantics follow xterm: wide characters occupy two cells (second is a
continuation), combining marks append to the preceding cell, deferred wrap waits
for the next printable character, and erase operations clear both halves of a
wide cell.

Scrolling the full main screen pushes the top line to scrollback; scrolling a
smaller DECSTBM region does not. Scrollback is trimmed from the front; if the
user is scrolled up when lines are trimmed, the viewport offset is adjusted.

## Renderer

`TerminalView` is a child HWND that paints with GDI into a double buffer.

- Monospaced font: Cascadia Mono if present, else Consolas. Regular/bold/
  italic/bold-italic faces, recreated on DPI or zoom change.
- Cell metrics come from `GetTextExtentPoint32W(L"M")` and `TEXTMETRIC`.
- Backgrounds are filled in contiguous runs; text is emitted in runs sharing
  foreground and attributes, falling back to single cells around wide glyphs.
  A run always records its own start column when it restarts after a gap of
  empty cells; reusing the previous run's offset would draw it shifted left
  (visible in TUIs such as opencode).
- Runs pass an explicit per-character advance (`cellWidth_`, zero for combining
  marks) and clip to the row instead of the run box, so fallback glyphs with
  negative bearings or overhang (box drawing, symbols, icons) keep their exact
  cell positions and are not cut off at program borders.
- Underline and strikethrough are drawn as 1-pixel lines; inverse swaps colors;
  invisible skips glyph drawing; blink is honored at render time.
- 16 ANSI colors, a 256-color cube, and 24-bit RGB are supported. Default
  foreground/background come from `theme.h`.
- Astral characters (emoji) are stored as surrogate pairs in one wide cell and
  drawn with GDI font linking; VS16 (`U+FE0F`) promotes a text-style symbol to
  two cells, and zero-width marks attach to the wide lead cell.
- Cursor: block, underline, or bar; optional blink; drawn only when focused and
  scrolled to the bottom.
- Selection: click-drag, double-click word, triple-click line; highlight and
  copy with trailing spaces trimmed. Ctrl+Shift+C / Ctrl+Shift+V, plus the menu.
- Scrollback: mouse wheel, Shift+wheel, Shift+PageUp/PageDown; an overlay
  scrollbar on the right; any keystroke returns to the bottom.
- Mouse reporting: when the app enables 1000/1002/1003, mouse events are encoded
  as SGR (1006) sequences and sent to the PTY. Shift overrides to local selection.
- IME: `WM_IME_COMPOSITION` result strings are sent to the PTY; the composition
  window is positioned at the cursor.

Focus changes send `ESC[I` / `ESC[O` when mode 1004 is enabled.

## Input encoding

`view.cpp` maps Win32 keys to xterm sequences, and honors Win32 input mode when
ConPTY requests it (`CSI ? 9001 h`):

- Win32 input mode: every key becomes `CSI Vk;Sc;Uc;Kd;Cs;Rc _`, including key
  releases, left/right modifier bits, locks, extended keys, and repeats. This is
  what makes Ctrl+Backspace, Ctrl+Delete, Alt+arrows, and the rest of the Windows
  key space behave exactly like Windows Terminal. Printable keys wait for
  `WM_CHAR` so dead-key and IME composition still work.
- Otherwise: classic xterm sequences for arrows/Home/End/Insert/Delete/PgUp/PgDn/
  F1–F12, Alt as an ESC prefix, and UTF-8 text from `WM_CHAR`.
- Ctrl+Shift shortcuts reserved for the app; everything else, including Ctrl+C,
  Ctrl+D, Ctrl+Z, reaches the shell
- paste normalizes CRLF to CR and honors bracketed paste mode
- resize recomputes the grid and calls `ResizePseudoConsole`

## UI chrome

Reuses the 67ed visual language exactly:

- One title/tab row (`kTabHeight` 28): burger menu on the left, tabs in the
  middle, fixed new-tab `+` on the right. No menu bar and no status bar.
- `theme::kBg` background, `kPanel` chrome, `kBorder` separators,
  `kText`/`kTextDim`/`kTextFaint`, one `kAccent`
- Custom-drawn tabs with close buttons, drag reorder, middle-click close
- Burger opens an owner-drawn dark popup; `File`/`Edit`/`View`/`Help` items open
  the corresponding section anchored under the burger. `Alt+F/E/V/H` do the same.
- `View > Font...` opens the fixed-pitch font chooser and applies family/size to
  every tab.
- `WM_NCCALCSIZE` removes the caption while keeping the resize frame;
  `WM_NCHITTEST` makes the title row draggable
- DWM immersive dark mode on a best-effort basis
- All metrics scaled with the window DPI; fonts recreated on DPI change

## Settings and persistence

Data directory is `%APPDATA%\67t\`, or `.\state\` in portable mode when
`67t.ini` sits next to the executable.

`settings.ini` keys:

```
font=Cascadia Mono
fontSize=16
shell=powershell.exe -NoLogo
scrollback=10000
cursorBlink=1
window=left,top,width,height,maximized
```

Writes are temp-file + `MoveFileExW` replacements. A corrupt or unreadable file
falls back to defaults without blocking startup.

## Testing

- `--selftest`: no visible window. Covers CLI, UTF-8, parser chunking, cursor,
  SGR, erase/edit commands, scroll region, alt screen, tab stops, wide chars,
  OSC title, response generation, scrollback, resize, key encoding, mouse
  encoding, paste, Win32 input-mode records, a real ConPTY spawn/echo/exit check,
  and a live PowerShell Ctrl+Backspace edit.
- `--bench`: parser/screen throughput on synthetic output.
- `--bench-startup`: hidden chrome construction timing and RSS.
- Test runner: `tests/run-selftest.ps1`.
