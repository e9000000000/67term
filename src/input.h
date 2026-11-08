#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace input {

struct KeyEvent {
    WPARAM key = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool applicationCursor = false;
};

// Returns an xterm sequence for arrows, Home/End, Insert/Delete, PgUp/PgDn,
// F1-F12, and Shift+Tab. Returns false for keys that are delivered through
// WM_CHAR instead.
bool EncodeKey(const KeyEvent& event, std::string& sequence);

std::string EncodeMouse(int button, int x, int y, bool release, bool motion, bool sgr,
                        int modifiers);

// Win32 Input Mode record: CSI Vk;Sc;Uc;Kd;Cs;Rc _
// (as requested by ConPTY through CSI ? 9001 h).
std::string EncodeWin32Key(int vk, int scanCode, int unicode, bool keyDown, int controlState,
                           int repeat);

std::wstring NormalizePaste(std::wstring_view text);

}  // namespace input
