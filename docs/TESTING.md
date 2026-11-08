# 67t — Testing

## Headless

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-bench.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-startup.ps1
```

The self-test creates no visible window. It writes `build\selftest.log` and exits
non-zero on a failure. Current check count: **105** (`summary passed=105 failed=0`).

Coverage:

- CLI option parsing and unknown-option errors
- UTF-8 encode/decode, wide and combining character widths
- emoji (astral) cells, VS16 promotion, modifiers, zero-width marks on wide lead cells
- explicit symbol/CJK font fallback coverage for tofu-prone code points
- screen print, CR/LF, CUP, erase, SGR indexed/truecolor/attributes, wide cells
- OSC title, OSC 7 directory, device attributes, cursor position report
- bracketed paste, application cursor keys, mouse modes, alternate screen
- scrollback growth/limit/offset, resize, split CSI sequences, invalid UTF-8
- DECALN, save/restore cursor, REP, tab stops, scroll region
- key encoding (arrows, modifiers, function keys, Shift+Tab), Win32 input records
- SGR/legacy mouse encoding and paste newline normalization
- real ConPTY: spawn, output, `TERM` environment, resize, child exit
- process current directory read from a live shell's PEB
- PowerShell OSC 7 integration: initial directory and following `cd`
- live PowerShell: Win32 input mode, Ctrl+Backspace, emoji input and the
  replacement-glyph follow-up Backspace
- hidden chrome construction timing

The PTY checks start `cmd.exe` inside a real pseudo console; no shell output is
scraped from a redirected console.

## Benchmarks

`--bench [lines]` builds colored, wide-character output and feeds it to the VT
engine, then measures a resize cycle and process RSS.

`--bench-startup` creates a hidden host window plus the burger/tab strip and
terminal view, and reports construction time and RSS.

Reference numbers (this machine, release build):

| Metric | Value |
|---|---|
| EXE | 553,984 bytes |
| 10,000 lines parse + store | 18.5 ms |
| Resize cycle (100x30 → 140x45 → 120x40) | 5.9 ms |
| RSS with 10k scrollback lines | ~50 MB |
| Hidden chrome construction | < 1 ms |

## Manual smoke

- Launch: prompt appears in under a second; title and tab show the shell.
- Type `echo hello`, arrows, backspace, `Ctrl+C`, `Ctrl+Backspace`, `Ctrl+Delete`;
  run `git status` for color.
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
- With a window open, launch `67t.exe` again: it exits and adds a tab to the
  running window. `67t.exe --cwd C:\Windows` opens the tab there, and
  `--shell cmd.exe` starts cmd in that tab. `--new-window` creates a second
  window instead. Close every window and launch again: a fresh primary starts.
- In a tab, `cd C:\Windows`, then use the plus button, `Ctrl+Shift+T`, and
  Duplicate Tab: each new tab must start in `C:\Windows` (this works without
  OSC 7 shell integration).
- Print or paste emoji (`🚀`, `✅`, `❤️`) and press Backspace: the emoji must
  render as one wide glyph, not two, and must disappear without leaving a
  replacement square.
- Default terminal: burger menu → **Make 67t the Default Terminal**, then launch
  `cmd.exe` from the Run dialog. It must open in a 67t tab (title `cmd`), show the
  prompt, echo typed commands, and close without leaving a 67t process behind.
- While that tab is open, confirm `Get-CimInstance Win32_Process` shows
  `OpenConsole.exe -Embedding` and `conhost.exe` under the client, and that
  `67t.exe -Embedding` has the terminal window.
- Burger menu → **Restore Previous Default Terminal** (or
  `67t.exe --restore-default-terminal --log build\restore.log`); verify
  `HKCU\Console\%%Startup` returns to the previous values.
- If Windows Terminal is not installed, **Make 67t the Default Terminal** must
  show a clear error instead of leaving the setting half-applied.
