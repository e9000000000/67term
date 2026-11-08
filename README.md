# 67t

[![Build](https://github.com/e9000000000/67t/actions/workflows/build.yml/badge.svg)](https://github.com/e9000000000/67t/actions/workflows/build.yml)

A native Windows terminal emulator: dark, tabbed, small, and powered by a real
ConPTY shell. Same visual language as [67ed](../67ed).

- Native Win32 + GDI + ConPTY, C++20, static build — one `.exe`
- Dark chrome: compact title/tab row with a burger menu and fixed new-tab button
- Real PowerShell / cmd / any console program, UTF-8 VT input and output
- Tabs with drag reorder, close buttons, OSC titles
- New tabs (plus button, `Ctrl+Shift+T`) start in the current tab's directory
- Launching 67t again opens a tab in the running window (`--new-window` for a
  separate window)
- 10,000-line scrollback (configurable), wheel and keyboard scrolling
- Scrollback search with highlighting (`Ctrl+Shift+F`); selections stay on
  their text while scrolling
- 16/256/24-bit color, bold/italic/underline/strike, cursor styles
- Selection, copy/paste, bracketed paste, mouse reporting, IME input
- UTF-8, wide characters and emoji (astral cells, VS16), alternate screen,
  scroll regions, DEC private modes
- Win32 input mode: Ctrl+Backspace, Ctrl+Delete, Alt+arrows and the rest of the
  Windows key space behave like Windows Terminal
- Resize reflows the ConPTY grid; close a tab and its shell is terminated
- Font chooser (fixed-pitch, size), font zoom, fullscreen
- Remembers window placement, font, scrollback, and cursor blink
- Can register itself as the Windows **default terminal** through the real
  console-delegation COM handoff, so `cmd`, PowerShell, and other console apps
  open in 67t — as tabs when a 67t window is already running

## Build

Requires Visual Studio Build Tools (MSVC), the Windows 10/11 SDK, and Windows 10
1809+ for ConPTY.

```powershell
.\build.bat
.\build\67t.exe
```

Debug build: `.\build.bat debug`. Clean: `.\build.bat clean`.

## Usage

```powershell
build\67t.exe                       # default shell (pwsh, powershell, or cmd)
build\67t.exe --shell cmd.exe
build\67t.exe --cwd C:\src
build\67t.exe C:\src                # a directory argument sets the start dir
build\67t.exe --set-default-terminal     # make 67t the Windows default terminal
build\67t.exe --restore-default-terminal # restore the previous setting
build\67t.exe --new-window           # force a separate window
```

A second launch adds a tab to the window that is already open (`--cwd` and
`--shell` are honored). Use `--new-window`, or **New Window** in the burger menu
(`Ctrl+Shift+N`), to get a separate window.

The two default-terminal actions are in the burger menu: **Make 67t the Default
Terminal** and **Restore Previous Default Terminal**.

## Shortcuts

| Action | Shortcut |
|---|---|
| New tab | `Ctrl+Shift+T` |
| New window | `Ctrl+Shift+N` |
| Close tab | `Ctrl+Shift+W` |
| Restart shell | `Ctrl+Shift+R` |
| Copy / paste | `Ctrl+Shift+C` / `Ctrl+Shift+V` |
| Select all | `Ctrl+Shift+A` |
| Find in scrollback | `Ctrl+Shift+F` |
| Clear scrollback | `Ctrl+Shift+K` |
| Next / previous tab | `Ctrl+Tab` / `Ctrl+Shift+Tab` |
| Go to tab 1–9 | `Ctrl+1` … `Ctrl+9` |
| Zoom in / out / reset | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
| Full screen | `F11` or `Alt+Enter` |
| Scroll back / forward | wheel, `Shift+PageUp` / `Shift+PageDown` |
| Scroll to top / bottom | `Ctrl+Shift+Home` / `Ctrl+Shift+End` |

`Ctrl+C`, `Ctrl+D`, `Ctrl+Z`, `Ctrl+Backspace`, `Ctrl+Delete`, arrows, function
keys, and the rest of the normal keyboard go to the shell. `View > Font...` picks
the terminal font and size.

## Settings

Settings live in `%APPDATA%\67t\settings.ini` (or `.\state\settings.ini` when
`67t.ini` sits next to the executable):

```ini
[67t]
font=Cascadia Mono
fontSize=16
shell=powershell.exe -NoLogo
scrollback=10000
cursorBlink=1
```

## Default terminal

Windows delegates new console sessions to a registered console host
(`OpenConsole.exe`) and then to a registered terminal via COM. 67t implements
`ITerminalHandoff3` as an out-of-proc COM local server (`67t.exe -Embedding`),
attaches to the handed-off ConPTY, and shows it in a normal tab. While a 67t
window is running it keeps the COM class registered, so later consoles (for
example launching a file that opens in Microsoft Edit) open as tabs there; the
first console starts the window.

Setting this up requires **Windows Terminal to be installed**: 67t uses Windows
Terminal's `OpenConsole.exe` as the console host and its
`OpenConsoleProxy.dll` as the marshalling proxy. The proxy is copied to
`%APPDATA%\67t\OpenConsoleProxy.dll` (the WindowsApps copy cannot be loaded
directly) and registered under `HKCU\Software\Classes`. No administrator rights
are needed, and the previous default is saved and restored from
`HKCU\Software\67t\DefaultTerminal`.

## Status

Under construction, but usable as a daily terminal. See
[`docs/PLAN.md`](docs/PLAN.md) for milestones and
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design.

Automated checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-selftest.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-bench.ps1
```

Measured on this machine: 554 KiB EXE, 18.5 ms to parse and store 10,000 colored
lines (120x40, 10k scrollback), 5.9 ms for a resize cycle, about 50 MB RSS with
10k scrollback lines.

## Not there yet

Scrollback search, split panes, OSC 8 hyperlinks, shell-integration marks, and
restoring tabs across restarts. 67t is not affiliated with Windows Terminal,
PowerShell, or Notepad.
