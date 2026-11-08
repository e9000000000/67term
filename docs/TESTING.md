# 67t — Testing

## Headless

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-bench.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-startup.ps1
```

The self-test creates no visible window. It writes `build\selftest.log` and exits
non-zero on a failure. Current check count: **73** (`summary passed=73 failed=0`).

Coverage:

- CLI option parsing and unknown-option errors
- UTF-8 encode/decode, wide and combining character widths
- screen print, CR/LF, CUP, erase, SGR indexed/truecolor/attributes, wide cells
- OSC title, OSC 7 directory, device attributes, cursor position report
- bracketed paste, application cursor keys, mouse modes, alternate screen
- scrollback growth/limit/offset, resize, split CSI sequences, invalid UTF-8
- DECALN, save/restore cursor, REP, tab stops, scroll region
- key encoding (arrows, modifiers, function keys, Shift+Tab)
- SGR/legacy mouse encoding and paste newline normalization
- real ConPTY: spawn, output, `TERM` environment, resize, child exit
- hidden chrome construction timing

The PTY checks start `cmd.exe` inside a real pseudo console; no shell output is
scraped from a redirected console.

## Benchmarks

`--bench [lines]` builds colored, wide-character output and feeds it to the VT
engine, then measures a resize cycle and process RSS.

`--bench-startup` creates a hidden host window plus the menu bar, tab bar, status
bar, and terminal view, and reports construction time and RSS.

Reference numbers (this machine, release build):

| Metric | Value |
|---|---|
| EXE | 460,800 bytes |
| 10,000 lines parse + store | 24.2 ms |
| Resize cycle (100x30 → 140x45 → 120x40) | 5.3 ms |
| RSS with 10k scrollback lines | ~50 MB |
| Hidden chrome construction | < 1 ms |

## Manual smoke

- Launch: prompt appears in under a second; title and tab show the shell.
- Type `echo hello`, arrows, backspace, `Ctrl+C`; run `git status` for color.
- Run a long command (`dir -Recurse C:\Windows`) and wheel/Shift+PageUp.
- Select text, `Ctrl+Shift+C`, paste into another app, `Ctrl+Shift+V` back.
- `Ctrl+Shift+T` twice, switch with `Ctrl+Tab`, drag tabs, middle-click/close.
- Type `exit`: tab shows `(exited)`; `Ctrl+Shift+R` restarts the shell.
- Resize the window and run `mode con` / `$Host.UI.RawUI.WindowSize`; check the
  reported size.
- Run a mouse-aware program if available (`more`, `vim`), verify clicks/wheel.
- DPI 125/150/200%: font and grid stay aligned; F11 toggles full screen.
- Drop a folder on the window: a new tab opens there.
- Close the app and relaunch: size/position and settings restore.
- Close a tab while a long-running process is active; no orphan child remains.
