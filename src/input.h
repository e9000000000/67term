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

std::wstring NormalizePaste(std::wstring_view text);

}  // namespace input
