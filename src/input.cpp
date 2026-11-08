#include "input.h"

#include <algorithm>
#include <string>

namespace input {
namespace {

int ModifierCode(const KeyEvent& event) {
    return 1 + (event.shift ? 1 : 0) + (event.alt ? 2 : 0) + (event.ctrl ? 4 : 0);
}

std::string Tilde(int code, int modifiers) {
    if (modifiers == 1) {
        return "\x1b[" + std::to_string(code) + "~";
    }
    return "\x1b[" + std::to_string(code) + ";" + std::to_string(modifiers) + "~";
}

std::string Letter(char letter, const KeyEvent& event, int modifiers) {
    if (modifiers == 1) {
        std::string out = event.applicationCursor ? "\x1bO" : "\x1b[";
        out.push_back(letter);
        return out;
    }
    std::string out = "\x1b[1;" + std::to_string(modifiers);
    out.push_back(letter);
    return out;
}

}  // namespace

bool EncodeKey(const KeyEvent& event, std::string& sequence) {
    const int modifiers = ModifierCode(event);
    switch (event.key) {
    case VK_UP:
        sequence = Letter('A', event, modifiers);
        return true;
    case VK_DOWN:
        sequence = Letter('B', event, modifiers);
        return true;
    case VK_RIGHT:
        sequence = Letter('C', event, modifiers);
        return true;
    case VK_LEFT:
        sequence = Letter('D', event, modifiers);
        return true;
    case VK_HOME:
        sequence = Letter('H', event, modifiers);
        return true;
    case VK_END:
        sequence = Letter('F', event, modifiers);
        return true;
    case VK_INSERT:
        sequence = Tilde(2, modifiers);
        return true;
    case VK_DELETE:
        sequence = Tilde(3, modifiers);
        return true;
    case VK_PRIOR:
        sequence = Tilde(5, modifiers);
        return true;
    case VK_NEXT:
        sequence = Tilde(6, modifiers);
        return true;
    case VK_F1:
        sequence = Letter('P', event, modifiers);
        return true;
    case VK_F2:
        sequence = Letter('Q', event, modifiers);
        return true;
    case VK_F3:
        sequence = Letter('R', event, modifiers);
        return true;
    case VK_F4:
        sequence = Letter('S', event, modifiers);
        return true;
    case VK_F5:
        sequence = Tilde(15, modifiers);
        return true;
    case VK_F6:
        sequence = Tilde(17, modifiers);
        return true;
    case VK_F7:
        sequence = Tilde(18, modifiers);
        return true;
    case VK_F8:
        sequence = Tilde(19, modifiers);
        return true;
    case VK_F9:
        sequence = Tilde(20, modifiers);
        return true;
    case VK_F10:
        sequence = Tilde(21, modifiers);
        return true;
    case VK_F11:
        sequence = Tilde(23, modifiers);
        return true;
    case VK_F12:
        sequence = Tilde(24, modifiers);
        return true;
    case VK_TAB:
        if (event.shift && !event.ctrl) {
            sequence = "\x1b[Z";
            return true;
        }
        return false;
    default:
        return false;
    }
}

std::string EncodeMouse(int button, int x, int y, bool release, bool motion, bool sgr,
                        int modifiers) {
    if (motion) {
        modifiers += 32;
    }
    if (sgr) {
        const int code = button + modifiers;
        return "\x1b[<" + std::to_string(code) + ";" + std::to_string(x + 1) + ";" +
               std::to_string(y + 1) + (release ? "m" : "M");
    }
    int code = release ? 3 + modifiers : button + modifiers;
    std::string out = "\x1b[M";
    out.push_back(static_cast<char>(32 + std::clamp(code, 0, 223)));
    out.push_back(static_cast<char>(32 + std::clamp(x + 1, 1, 223)));
    out.push_back(static_cast<char>(32 + std::clamp(y + 1, 1, 223)));
    return out;
}

std::string EncodeWin32Key(int vk, int scanCode, int unicode, bool keyDown, int controlState,
                           int repeat) {
    return "\x1b[" + std::to_string(vk) + ";" + std::to_string(scanCode) + ";" +
           std::to_string(unicode) + ";" + (keyDown ? "1" : "0") + ";" +
           std::to_string(controlState) + ";" + std::to_string(std::max(1, repeat)) + "_";
}

std::wstring NormalizePaste(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r') {
            out.push_back(L'\r');
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace input
