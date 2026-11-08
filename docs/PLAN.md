# 67t — Plan

## Confirmed decisions

- **Native Windows terminal emulator**, x64, Windows 10 1809+/Windows 11.
  No browser, no webview, no HTTP.
- **C++20 + Win32 + GDI + ConPTY**, static CRT, single EXE.
- **Second launches join the running window as a new tab** and honor `--cwd` and
  `--shell`; `--new-window` forces a second window. Delegated default-terminal
  sessions are always their own window.
- **Tabs** are in the compact custom title row, matching 67ed.
- Each tab owns one ConPTY session, one child shell, one screen buffer, and one
  terminal child window.
- **Default shell**: `powershell.exe -NoLogo` when available, else
  `%COMSPEC%` (`cmd.exe`). Overridable per launch and in settings.
- **Scrollback** is in-memory, bounded (default 10,000 lines), with trimming.
- **Not in v1**: split panes, SSH profiles, Sixel/Kitty graphics, ligatures,
  tabs restored across restarts, background transparency/acrylic, non-Windows
  builds.

## Progress

- **M0 complete (2026-11-08).** Repo, build script, CI, custom dark chrome,
  ConPTY sessions with bounded reader queue and process-exit detection, UTF-8 VT
  parser, screen buffer with scrollback and wide cells, GDI renderer, keyboard,
  mouse, IME, and headless tests.
- **M1 complete (2026-11-08).** Tabs (new/close/reorder/context menu, OSC
  titles), Ctrl+Shift shortcuts, tab cycling, font zoom, fullscreen, font
  chooser, burger menu, settings and window-placement persistence, drag-and-drop,
  and shell exit/restart.
- **M2 in progress.** OSC 7 directories, OSC 52 clipboard writes, SGR mouse,
  Win32 input mode (Ctrl+Backspace/Ctrl+Delete), word/line selection, scrollback
  bar, Shift+PageUp/PageDown, Windows default-terminal registration
  (`ITerminalHandoff3` COM handoff, with delegated consoles opening as tabs),
  single-instance tab forwarding, new tabs inheriting the current directory
  (OSC 7 or shell PEB), and emoji/VS16 width handling are done. A renderer bug
  that shifted text after blank cells left is fixed. Remaining: scrollback
  search, OSC 8 hyperlinks, shell-integration marks, and optional
  focus/attention polish.
- **M3 not started.**

Latest measured release build: 531,456 bytes; 10,000 colored lines parsed and
stored in 18.5 ms; resize cycle 5.9 ms; ~50 MB RSS with 10k scrollback lines.

## Target numbers

| Metric | Target |
|---|---|
| EXE size | < 3 MB |
| Window visible (cold) | < 100 ms |
| Keypress to PTY write | < 2 ms |
| 10k output lines parse+screen | < 150 ms |
| Resize 120x40 with 10k scrollback | no visible stall |
| RSS with 10k scrollback lines | < 80 MB |
| Idle CPU | effectively 0% |

## Milestones

### M0 — Native skeleton and real shell

- Build script, repo, resources, CI.
- Custom dark window chrome: title/tab row with burger menu and fixed plus; no
  separate menu bar or status bar.
- ConPTY session: spawn shell, reader thread, input writes, resize, clean close.
- VT parser: UTF-8, C0, ESC, CSI, OSC, private modes, SGR, erase/insert/delete,
  scroll regions, alt screen, tab stops, DA/DSR responses.
- Screen buffer: wide chars, scrollback, cursor, selection, mouse modes.
- Renderer: monospaced grid, 16/256/truecolor, bold/italic/underline/strike,
  cursor, selection, scrollback bar.
- Keyboard: printable, control, arrows, Home/End/PgUp/PgDn, F1–F12, Alt meta,
  bracketed paste. Mouse reporting. IME minimal.

**Done when:** `67t.exe` opens a dark window running PowerShell, `dir` works,
colors show, arrows/Ctrl+C work, output scrolls, copy/paste works, resize reflows
the PTY, and closing the tab kills the shell.

### M1 — Tabs and daily driving

- New/close/reorder tabs, tab context menu, tab titles from OSC 0/2.
- Ctrl+Shift+T/W, Ctrl+Tab cycling, duplicate tab in same directory.
- Font size zoom, fullscreen (F11), window placement persistence.
- Settings INI: shell, font family/size, scrollback, cursor blink.
- Drag-and-drop folder opens a tab there; file drop inserts the path.
- Process-exit state with a restart action.

**Done when:** several shells can run side by side, tab titles update, closing a
tab never leaves an orphan process, and restarting the app restores size/position
and font settings.

### M2 — Terminal completeness

- Scrollback search (Ctrl+Shift+F) with match highlight and next/prev.
- OSC 7 working-directory tracking; new tab inherits it.
- OSC 52 clipboard writes, OSC 8 hyperlinks (visual + click), bell/attention.
- Mouse: SGR, button/motion modes, Shift override for selection.
- Selection improvements: word/line selection on double/triple click, auto-scroll.
- Shell integration marks (OSC 133) for scroll-to-prompt, optional.
- Windows default-terminal registration through console delegation
  (`ITerminalHandoff3`), with save/restore of the previous setting.

**Done when:** common TUI programs (vim, less, htop if present) render and respond
to keys and mouse; search is usable on 10k lines.

### M3 — Polish and packaging

- DPI pass at 125/150/200%, re-measure font cell metrics on DPI change.
- Visual pass against 67ed: spacing, focus states, menu alignment.
- Crash log and graceful ConPTY failure messages.
- Bench suite and self-test coverage for the whole VT surface.
- CI artifacts and releases like 67ed.

**Done when:** one portable `67t.exe`, target numbers met, no light chrome.

## Recovery policy

Terminal buffers are not restored across restarts. Closing a window or tab is
explicit: the child is terminated, the pseudo console is closed, and pipes are
released. A crash cannot resurrect a shell; nothing is silently re-run.

## Out of scope (v1)

Split panes, SSH/config profiles, GPU rendering, ligatures, background images,
acrylic, Sixel/Kitty graphics, tab restore, remote sessions, macOS/Linux UI.
