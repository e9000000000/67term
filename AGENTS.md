# AGENTS.md — 67t

67t is a native Windows terminal emulator: one small executable, dark UI, tabs,
real shells through ConPTY, scrollback, selection, and mouse reporting. It is
**not** a web app. There is no browser, no HTTP server, no WebView, no Electron or
Tauri, and no telemetry or network access at runtime.

The look and interaction model follow the sibling project `../67ed`: compact
custom title/tab row with a burger menu, one blue accent, small native Win32
controls, no installer, no menu bar or status bar.

## Current state

M0 and M1 are complete, and M2 is partly done. The app builds as one x64
executable (~500 KiB) and runs real shells through ConPTY. It has: custom dark
title/tab chrome with a burger menu and fixed new-tab button, multiple tabs with
drag reorder and context menus, a UTF-8 VT parser with 16/256/24-bit color, wide
characters, alternate screen, scroll regions and scrollback, a GDI renderer with
cursor styles and selection, keyboard/mouse/IME input (including Win32 input
mode), bracketed paste, shell-exit detection and restart, font settings, font
zoom, fullscreen, settings/window persistence, drag-and-drop, OSC titles, OSC 7
working directories and OSC 52 clipboard writes.

67t can also register itself as the Windows **default terminal**. It implements
`ITerminalHandoff3` as an out-of-proc COM local server (`67t.exe -Embedding`) and
attaches to the ConPTY session handed off by Windows. This uses Windows
Terminal's `OpenConsole.exe` console host and copies its
`OpenConsoleProxy.dll` to `%APPDATA%\67t` for proxy/stub registration. The menu
action and the CLI flags save and restore the previous default; no admin rights
are needed.

Every 67t window keeps its COM class registered with `REGCLS_MULTIPLEUSE`, so
once a window is running Windows hands it new delegated consoles (for example
`edit.exe` or a shortcut that starts a console app) and they open as tabs.
With no window running, COM starts `67t.exe -Embedding`, which opens the first
window and then serves later handoffs the same way.

Launching 67t again while a window is open forwards the request to that window
and opens a new tab there (honoring `--cwd` and `--shell`). `--new-window`
opts out and starts a second window; delegated default-terminal sessions are
always their own window.

New tabs (the plus button, `Ctrl+Shift+T`, and Duplicate Tab) start in the
active tab's directory. OSC 7 is used when the shell reports it; otherwise the
shell process's current directory is read from its PEB, so shells without
integration still inherit the directory.

Unicode: astral characters are stored as surrogate pairs in one wide cell,
emoji-presentation code points are width 2, and VS16 (`U+FE0F`) promotes
text-style symbols to wide emoji. Missing glyphs are resolved through explicit
system fallbacks (Segoe UI Symbol, Segoe UI Emoji, Microsoft YaHei UI,
Consolas) instead of relying on GDI font linking, which does not cover every
symbol. Astral input is sent as UTF-8 text instead of
Win32 key records, because a record only holds one UTF-16 code unit. Windows
PowerShell 5.1 deletes one code unit per Backspace and leaves `U+FFFD`; the view
detects that replacement glyph and sends one follow-up Backspace.

Known gaps: scrollback search, split panes, OSC 8 hyperlinks, shell-integration
marks, and restoring tabs across restarts. Read `docs/PLAN.md` for milestones and
`docs/ARCHITECTURE.md` for the design.

## Build

Requirements: Windows 10 1809+ (ConPTY) / Windows 11 x64, Visual Studio Build
Tools (MSVC 19.3x+), Windows SDK 10.0.26100+.

```powershell
.\build.bat            # release, build\67t.exe
.\build.bat debug      # debug, build\67t-debug.exe
.\build.bat clean      # delete build/
```

`build.bat` locates Visual Studio with `vswhere`, imports `vcvars64.bat`,
compiles resources, and links one EXE with the static CRT (`/MT`). Do not add a
runtime DLL, installer, package manager, or third-party runtime dependency.

## Run

```powershell
build\67t.exe                       # new tab in the default shell
build\67t.exe --shell cmd.exe       # override the shell for this launch
build\67t.exe --cwd C:\src          # start directory
build\67t.exe C:\src                # a directory argument becomes the start dir
build\67t.exe --set-default-terminal     # make 67t the Windows default terminal
build\67t.exe --restore-default-terminal # restore the saved previous default
build\67t.exe --new-window           # force a separate window
build\67t.exe --help
```

`--set-default-terminal` and `--restore-default-terminal` write a status line to
`--log <path>` when one is given; otherwise they show a message box.

A second launch forwards its working directory and optional shell to the running
window and opens a new tab. `--new-window` forces a separate window. Delegated
default-terminal sessions are always separate windows.

## Layout

```
src/
  main.cpp          wWinMain, CLI dispatch, headless modes, crash log install
  cli.*             command-line parsing
  accel.*           application accelerators
  commands.h        command IDs shared by menu, accelerators, app
  app.*             main window, tabs, chrome layout, commands, menus
  menubar.*         hidden host for owner-drawn dark popup menus
  tabbar.*          title/tab strip: burger menu, tabs, fixed new-tab button
  theme.h           palette, metrics, DPI helpers
  util.*            UTF conversions, paths, errors, dark-mode helpers
  settings.*        settings.ini and window-placement persistence
  singleinstance.*  forward a second launch to the running window as a new tab
  pty.*             ConPTY creation, attached sessions, reader/writer threads, resize
  defterm.*         default-terminal registry setup, proxy install, set/restore
  handoff.*         COM local server for ITerminalHandoff3 (default terminal)
  terminal.*        screen buffer, VT parser, scrollback, modes, responses
  input.*           xterm key/mouse sequence encoding, paste normalization
  view.*            terminal child window: renderer, keyboard, mouse, IME
  headless.*        --selftest / --bench / --bench-startup, no visible UI
resources/
  app.rc            icon, manifest, version
  app.manifest      per-monitor DPI, common controls v6, ConPTY-friendly
  app.ico
docs/
  PLAN.md           milestones and acceptance criteria
  ARCHITECTURE.md   detailed design
  TESTING.md        headless checks and manual smoke list
tests/
  run-selftest.ps1
.github/
  workflows/build.yml  CI build, artifact, rolling/versioned releases
```

## Non-negotiable rules

1. **Single EXE.** Static CRT (`/MT`), no DLLs, no installer, no runtime
   download. Windows system DLLs only.
2. **Native only.** Win32 + GDI + ConPTY. No webview, no managed runtime, no
   Electron/Tauri.
3. **Real terminal.** The shell process runs in a ConPTY. Never fake a prompt,
   never pipe a shell through a hidden console and scrape text.
4. **UI thread owns the screen.** PTY bytes are read on a worker and handed to
   the UI thread; the screen buffer, parser, and rendering are UI-thread only.
   Never paint or mutate terminal state from the reader thread.
5. **Bounded queues.** PTY output backlog is capped; if the UI cannot keep up,
   apply backpressure or drop with a visible warning rather than growing without
   limit.
6. **Correct cleanup.** Closing a tab terminates its child process, closes the
   pseudo console and pipes, and joins the reader thread. No leaked handles.
7. **Unicode everywhere.** Build with `UNICODE`/`_UNICODE`. Shell I/O is UTF-8;
   paths and UI text are `std::wstring`.
8. **No per-keystroke IPC.** Keystrokes are encoded and written to the PTY pipe
   directly.
9. **Atomic state writes.** Settings and window placement are written to a temp
   file then renamed.
10. **No telemetry, no network, no auto-update.**

## Performance budgets

Measure with `67t.exe --bench-startup` and the self-test timings:

- EXE size: under 3 MB.
- Window visible: under 100 ms from process start.
- Keypress to PTY write: under 2 ms on the UI thread.
- 10,000 lines of output: parser + screen under 150 ms.
- Idle CPU at rest: effectively 0% (no busy polling).
- Resize: no visible stall for 120x40 with 10k scrollback lines.
- Memory: under 80 MB with 10k scrollback lines at 120x40.

Do not regress these without updating the plan and recording numbers.

Latest measured release build: 541,696-byte EXE; 10,000 colored lines parsed and
stored in 18.5 ms; resize cycle 5.9 ms; ~50 MB RSS with 10k scrollback lines;
hidden chrome construction under 1 ms.

## Conventions

- C++20, `/W4`, RAII, `std::wstring` for UI text, `std::string` for UTF-8 PTY
  bytes.
- Colors and metrics live in `src/theme.h`; never hard-code random RGB values in
  widgets.
- The VT engine is independent of Win32: `terminal.*` must be testable headless.
- Renderer optimizations must not change cell semantics. Prefer run batching over
  per-cell `ExtTextOutW` when easy, but correctness first.
- Pointer and handle ownership: `PtySession` owns `HPCON`, child/thread handles;
  `TerminalView` owns its `Terminal`; `App` owns tabs/views.
- Error messages must include the failing path and the Win32 error text.
- Terminal responses (DA, DSR, window size) go through a response callback; the
  parser never writes to the pipe itself.

## Testing

All automated testing is headless. `--selftest`, `--bench`, and
`--bench-startup` create no visible terminal window.

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
```

- `--selftest`: CLI parsing, UTF-8, VT parser across chunk boundaries, cursor
  movement, SGR colors, erase/insert/delete, scroll regions, alt screen, tab
  stops, wide characters, emoji width and VS16 promotion, OSC titles, DA/DSR
  responses, scrollback trimming, resize, key encoding, mouse/paste encoding,
  Win32 input-mode records, ConPTY spawn + echo + exit, process current
  directory, live PowerShell Ctrl+Backspace editing, emoji input, the
  replacement-glyph cleanup, and symbol/CJK font fallback coverage. Results are
  JSON-ish lines; expect `summary passed=101 failed=0`.
- `--bench [lines]`: parser/screen microbenchmarks on synthetic colored output.
- `--bench-startup`: hidden chrome construction timing and RSS.
- Manual smoke: PowerShell and cmd tabs, `dir`, `git status`, colors, Unicode,
  resize, scrollback wheel, select/copy/paste, Ctrl+C, Ctrl+Backspace/Ctrl+Delete,
  arrows in a REPL, mouse
  reporting (`more`, vim if installed), DPI 125/150/200%, close tab while a
  process runs, exit the shell and restart the tab.
- Single instance: run `67t.exe` while a window is open; it must exit silently
  and add a tab to the existing window. Repeat with `--cwd C:\Windows` and
  `--shell cmd.exe`; the new tab must use them. `--new-window` must start a
  second window.
- Default terminal: `--set-default-terminal`, then start `cmd.exe` from the Run
  dialog while a 67t window is open; it must open as a tab in that window.
  Repeat with no 67t running: the first console must open a window, and the
  next one a tab in it. The session must echo typed input and exit cleanly. Check
  the process tree has `OpenConsole.exe` under `svchost.exe`, then run
  `--restore-default-terminal` and confirm `HKCU\Console\%%Startup` is back.

## Known pitfalls

- ConPTY is only present on Windows 10 1809+. On older systems, show a clear
  error and exit; do not silently degrade.
- ConPTY may emit partial UTF-8 sequences across reads; the parser must keep
  decoder state across feeds.
- `ClosePseudoConsole` can block briefly while the client tears down. Never call
  it from the reader thread.
- ConPTY requests Win32 input mode (`CSI ? 9001 h`). We honor it: key-down/up
  records carry VK, scan code, Unicode, control state, and repeats. If the mode
  is disabled, fall back to classic xterm key sequences.
- Some Windows 10 builds wrap output in extra VT sequences; ignore unknown
  private modes and CSI commands rather than failing.
- Selection should preserve wide characters and not copy continuation cells.
- DWM dark title bar is best effort; the app chrome itself must always be dark.
- Default-terminal handoff needs Windows Terminal's `OpenConsole.exe` and
  `OpenConsoleProxy.dll`. The WindowsApps copy cannot be loaded directly
  (`LoadLibrary` returns access denied), so it is copied to `%APPDATA%\67t` and
  registered from there.
- The handed-off terminal pipe **must** be opened for overlapped I/O on both
  ends. A synchronous duplex pipe receives the ConPTY handshake and then stalls;
  ConPTY uses overlapped reads/writes on its end.
- The COM server is launched as `67t.exe -Embedding`; it waits up to 15 seconds
  for a handoff, then exits without showing a window if none arrives.
- The terminal class must be registered with `REGCLS_MULTIPLEUSE` and kept
  registered for the window's lifetime. Registering single-use (or revoking
  after the first handoff) makes every later delegated console start a new
  window instead of a tab.
- Renderer text runs must record their own start column whenever a run restarts
  (for example after a gap of empty cells). Reusing the previous run's start
  offset draws the text shifted left, which is visible in TUIs such as
  opencode.
- Runs pass an explicit `lpDx` advance (`cellWidth_`, `0` for combining marks)
  and clip to the whole row rather than the run box. Fallback fonts for box
  drawing, symbols and icons have negative side bearings and overhang their
  cell; clipping to the run used to cut them off at program borders.
- Win32 input-mode records carry one UTF-16 code unit (`WORD`), not a code
  point. Send astral characters as UTF-8 text instead of records; surrogate
  halves sent as records are dropped by console hosts.
- Windows PowerShell 5.1 (PSReadLine) removes one UTF-16 unit per Backspace, so
  deleting half of an emoji leaves a lone surrogate rendered as `U+FFFD`. The
  view arms a check when Backspace follows an astral cell and, if `U+FFFD`
  appears there within ~1.5 s, sends one more `0x7F`. Do not blindly send a
  double Backspace: apps that delete a whole code point would lose an extra
  character.
- Zero-width marks must attach to the wide *lead* cell, not its continuation
  cell. VS16 (`U+FE0F`) promotes a text-style symbol to a two-cell emoji.
- Do not trust GDI's implicit font linking for symbols. Some fonts (Hack,
  Consolas) lack characters such as `U+2B1D`, and linking then leaves a tofu
  box. Cache `GetFontUnicodeRanges` per font and select an explicit fallback;
  `GLYPHSET` covers only the BMP, so astral characters use the emoji face.
- `ProcessCurrentDirectory` reads `RTL_USER_PROCESS_PARAMETERS` through the
  PEB; x64 uses offsets 0x20/0x38 and WOW64 targets use 0x10/0x24.
- Single-instance forwarding uses a hidden top-level window (`67tInstanceHost`)
  found with `FindWindowW` plus a named mutex to serialize startup. Message-only
  windows (`HWND_MESSAGE`) are invisible to `FindWindowW`, so the receiver must
  stay a normal hidden window.
