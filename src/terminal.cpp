#include "terminal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace vt {
namespace {

int ClampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

bool IsCombining(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) ||
           (cp >= 0x0591 && cp <= 0x05BD) || (cp >= 0x0610 && cp <= 0x061A) ||
           (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x0670 && cp <= 0x0670) ||
           (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) ||
           (cp >= 0x06E7 && cp <= 0x06E8) || (cp >= 0x06EA && cp <= 0x06ED) ||
           (cp >= 0x0711 && cp <= 0x0711) || (cp >= 0x0730 && cp <= 0x074A) ||
           (cp >= 0x07A6 && cp <= 0x07B0) || (cp >= 0x07EB && cp <= 0x07F3) ||
           (cp >= 0x0816 && cp <= 0x082D) || (cp >= 0x0859 && cp <= 0x085B) ||
           (cp >= 0x08D3 && cp <= 0x0903) || (cp >= 0x093A && cp <= 0x094F) ||
           (cp >= 0x0951 && cp <= 0x0957) || (cp >= 0x0962 && cp <= 0x0963) ||
           (cp >= 0x0981 && cp <= 0x0983) || (cp >= 0x09BC && cp <= 0x09CD) ||
           (cp >= 0x09D7 && cp <= 0x09D7) || (cp >= 0x09E2 && cp <= 0x09E3) ||
           (cp >= 0x0A01 && cp <= 0x0A03) || (cp >= 0x0A3C && cp <= 0x0A51) ||
           (cp >= 0x0A70 && cp <= 0x0A71) || (cp >= 0x0A75 && cp <= 0x0A75) ||
           (cp >= 0x0A81 && cp <= 0x0A83) || (cp >= 0x0ABC && cp <= 0x0ACD) ||
           (cp >= 0x0AE2 && cp <= 0x0AE3) || (cp >= 0x0B01 && cp <= 0x0B03) ||
           (cp >= 0x0B3C && cp <= 0x0B4D) || (cp >= 0x0B56 && cp <= 0x0B57) ||
           (cp >= 0x0B82 && cp <= 0x0B82) || (cp >= 0x0BBE && cp <= 0x0BCD) ||
           (cp >= 0x0C00 && cp <= 0x0C04) || (cp >= 0x0C3E && cp <= 0x0C56) ||
           (cp >= 0x0C81 && cp <= 0x0C83) || (cp >= 0x0CBC && cp <= 0x0CD6) ||
           (cp >= 0x0D00 && cp <= 0x0D03) || (cp >= 0x0D3B && cp <= 0x0D4D) ||
           (cp >= 0x0D81 && cp <= 0x0D83) || (cp >= 0x0DCA && cp <= 0x0DF3) ||
           (cp >= 0x0E31 && cp <= 0x0E31) || (cp >= 0x0E34 && cp <= 0x0E3A) ||
           (cp >= 0x0E47 && cp <= 0x0E4E) || (cp >= 0x0EB1 && cp <= 0x0EB1) ||
           (cp >= 0x0EB4 && cp <= 0x0EBC) || (cp >= 0x0EC8 && cp <= 0x0ECD) ||
           (cp >= 0x0F18 && cp <= 0x0F19) || (cp >= 0x0F35 && cp <= 0x0F35) ||
           (cp >= 0x0F37 && cp <= 0x0F37) || (cp >= 0x0F39 && cp <= 0x0F39) ||
           (cp >= 0x0F71 && cp <= 0x0F84) || (cp >= 0x0F86 && cp <= 0x0F87) ||
           (cp >= 0x0F8D && cp <= 0x0FBC) || (cp >= 0x102D && cp <= 0x103E) ||
           (cp >= 0x1058 && cp <= 0x1059) || (cp >= 0x105E && cp <= 0x1060) ||
           (cp >= 0x1071 && cp <= 0x1074) || (cp >= 0x1082 && cp <= 0x108D) ||
           (cp >= 0x109D && cp <= 0x109D) || (cp >= 0x135D && cp <= 0x135F) ||
           (cp >= 0x1712 && cp <= 0x1714) || (cp >= 0x1732 && cp <= 0x1734) ||
           (cp >= 0x1752 && cp <= 0x1753) || (cp >= 0x1772 && cp <= 0x1773) ||
           (cp >= 0x17B4 && cp <= 0x17D3) || (cp >= 0x17DD && cp <= 0x17DD) ||
           (cp >= 0x180B && cp <= 0x180D) || (cp >= 0x1885 && cp <= 0x1886) ||
           (cp >= 0x18A9 && cp <= 0x18A9) || (cp >= 0x1920 && cp <= 0x1933) ||
           (cp >= 0x1939 && cp <= 0x193B) || (cp >= 0x1A17 && cp <= 0x1A1B) ||
           (cp >= 0x1A58 && cp <= 0x1A7F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1B00 && cp <= 0x1B04) || (cp >= 0x1B34 && cp <= 0x1B44) ||
           (cp >= 0x1B6B && cp <= 0x1B73) || (cp >= 0x1B80 && cp <= 0x1B82) ||
           (cp >= 0x1BA1 && cp <= 0x1BAD) || (cp >= 0x1BE6 && cp <= 0x1BF3) ||
           (cp >= 0x1C24 && cp <= 0x1C37) || (cp >= 0x1CD0 && cp <= 0x1CD2) ||
           (cp >= 0x1CD4 && cp <= 0x1CE8) || (cp >= 0x1CED && cp <= 0x1CED) ||
           (cp >= 0x1CF4 && cp <= 0x1CF4) || (cp >= 0x1CF8 && cp <= 0x1CF9) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0x2CEF && cp <= 0x2CF1) || (cp >= 0x2D7F && cp <= 0x2D7F) ||
           (cp >= 0x2DE0 && cp <= 0x2DFF) || (cp >= 0x302A && cp <= 0x302F) ||
           (cp >= 0x3099 && cp <= 0x309A) || (cp >= 0xA66F && cp <= 0xA672) ||
           (cp >= 0xA674 && cp <= 0xA67D) || (cp >= 0xA69E && cp <= 0xA69F) ||
           (cp >= 0xA6F0 && cp <= 0xA6F1) || (cp >= 0xA802 && cp <= 0xA802) ||
           (cp >= 0xA806 && cp <= 0xA806) || (cp >= 0xA80B && cp <= 0xA80B) ||
           (cp >= 0xA823 && cp <= 0xA827) || (cp >= 0xA880 && cp <= 0xA881) ||
           (cp >= 0xA8B4 && cp <= 0xA8C5) || (cp >= 0xA8E0 && cp <= 0xA8F1) ||
           (cp >= 0xA926 && cp <= 0xA92D) || (cp >= 0xA947 && cp <= 0xA953) ||
           (cp >= 0xA980 && cp <= 0xA983) || (cp >= 0xA9B3 && cp <= 0xA9C0) ||
           (cp >= 0xA9E5 && cp <= 0xA9E5) || (cp >= 0xAA29 && cp <= 0xAA36) ||
           (cp >= 0xAA43 && cp <= 0xAA43) || (cp >= 0xAA4C && cp <= 0xAA4D) ||
           (cp >= 0xAA7B && cp <= 0xAA7D) || (cp >= 0xAAB0 && cp <= 0xAAB0) ||
           (cp >= 0xAAB2 && cp <= 0xAAB4) || (cp >= 0xAAB7 && cp <= 0xAAB8) ||
           (cp >= 0xAABE && cp <= 0xAABF) || (cp >= 0xAAC1 && cp <= 0xAAC1) ||
           (cp >= 0xAAEB && cp <= 0xAAEF) || (cp >= 0xAAF5 && cp <= 0xAAF6) ||
           (cp >= 0xABE3 && cp <= 0xABEA) || (cp >= 0xABEC && cp <= 0xABED) ||
           (cp >= 0xFB1E && cp <= 0xFB1E) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x101FD && cp <= 0x101FD) ||
           (cp >= 0x102E0 && cp <= 0x102E0) || (cp >= 0x10376 && cp <= 0x1037A) ||
           (cp >= 0x10A01 && cp <= 0x10A0F) || (cp >= 0x10A38 && cp <= 0x10A3F) ||
           (cp >= 0x10AE5 && cp <= 0x10AE6) || (cp >= 0x11000 && cp <= 0x11002) ||
           (cp >= 0x11038 && cp <= 0x11046) || (cp >= 0x1107F && cp <= 0x11082) ||
           (cp >= 0x110B0 && cp <= 0x110BA) || (cp >= 0x11100 && cp <= 0x11102) ||
           (cp >= 0x11127 && cp <= 0x11134) || (cp >= 0x11173 && cp <= 0x11173) ||
           (cp >= 0x11180 && cp <= 0x11182) || (cp >= 0x111B3 && cp <= 0x111C0) ||
           (cp >= 0x1122C && cp <= 0x11237) || (cp >= 0x112DF && cp <= 0x112EA) ||
           (cp >= 0x11300 && cp <= 0x11303) || (cp >= 0x1133B && cp <= 0x1134D) ||
           (cp >= 0x11357 && cp <= 0x11357) || (cp >= 0x11362 && cp <= 0x11374) ||
           (cp >= 0x114B0 && cp <= 0x114C3) || (cp >= 0x115AF && cp <= 0x115C0) ||
           (cp >= 0x11630 && cp <= 0x11640) || (cp >= 0x116AB && cp <= 0x116B7) ||
           (cp >= 0x1171D && cp <= 0x1172B) || (cp >= 0x11C2F && cp <= 0x11C3F) ||
           (cp >= 0x16AF0 && cp <= 0x16AF4) || (cp >= 0x16B30 && cp <= 0x16B36) ||
           (cp >= 0x1BC9D && cp <= 0x1BC9E) || (cp >= 0x1D165 && cp <= 0x1D169) ||
           (cp >= 0x1D16D && cp <= 0x1D182) || (cp >= 0x1D185 && cp <= 0x1D18B) ||
           (cp >= 0x1D1AA && cp <= 0x1D1AD) || (cp >= 0x1D242 && cp <= 0x1D244) ||
           (cp >= 0x1DA00 && cp <= 0x1DA36) || (cp >= 0x1DA3B && cp <= 0x1DA6C) ||
           (cp >= 0x1DA75 && cp <= 0x1DA75) || (cp >= 0x1DA84 && cp <= 0x1DA84) ||
           (cp >= 0x1DA9B && cp <= 0x1DA9F) || (cp >= 0x1DAA1 && cp <= 0x1DAAF) ||
           (cp >= 0x1E000 && cp <= 0x1E02A) || (cp >= 0x1E8D0 && cp <= 0x1E8D6) ||
           (cp >= 0x1E944 && cp <= 0x1E94A) || (cp >= 0xE0100 && cp <= 0xE01EF) ||
           cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 ||
           cp == 0xFEFF || cp == 0x00AD;
}

bool IsWide(uint32_t cp) {
    if (cp < 0x1100) {
        return false;
    }
    if ((cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xA000 && cp <= 0xA4CF) || (cp >= 0xA960 && cp <= 0xA97F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F004 && cp <= 0x1F004) || (cp >= 0x1F0CF && cp <= 0x1F0CF) ||
        (cp >= 0x1F18E && cp <= 0x1F18E) || (cp >= 0x1F191 && cp <= 0x1F19A) ||
        (cp >= 0x1F200 && cp <= 0x1F320) || (cp >= 0x1F32D && cp <= 0x1F335) ||
        (cp >= 0x1F337 && cp <= 0x1F37C) || (cp >= 0x1F37E && cp <= 0x1F393) ||
        (cp >= 0x1F3A0 && cp <= 0x1F3CA) || (cp >= 0x1F3CF && cp <= 0x1F3D3) ||
        (cp >= 0x1F3E0 && cp <= 0x1F3F0) || (cp >= 0x1F3F4 && cp <= 0x1F3F4) ||
        (cp >= 0x1F3F8 && cp <= 0x1F43E) || (cp >= 0x1F440 && cp <= 0x1F440) ||
        (cp >= 0x1F442 && cp <= 0x1F4FC) || (cp >= 0x1F4FF && cp <= 0x1F53D) ||
        (cp >= 0x1F54B && cp <= 0x1F54E) || (cp >= 0x1F550 && cp <= 0x1F567) ||
        (cp >= 0x1F57A && cp <= 0x1F57A) || (cp >= 0x1F595 && cp <= 0x1F596) ||
        (cp >= 0x1F5A4 && cp <= 0x1F5A4) || (cp >= 0x1F5FB && cp <= 0x1F64F) ||
        (cp >= 0x1F680 && cp <= 0x1F6C5) || (cp >= 0x1F6CC && cp <= 0x1F6CC) ||
        (cp >= 0x1F6D0 && cp <= 0x1F6D2) || (cp >= 0x1F6D5 && cp <= 0x1F6D7) ||
        (cp >= 0x1F6EB && cp <= 0x1F6EC) || (cp >= 0x1F6F4 && cp <= 0x1F6FC) ||
        (cp >= 0x1F7E0 && cp <= 0x1F7EB) || (cp >= 0x1F90C && cp <= 0x1F93A) ||
        (cp >= 0x1F93C && cp <= 0x1F945) || (cp >= 0x1F947 && cp <= 0x1F9FF) ||
        (cp >= 0x1FA70 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x2FFFD) ||
        (cp >= 0x30000 && cp <= 0x3FFFD)) {
        return true;
    }
    return false;
}

std::wstring Utf8ToWideLocal(std::string_view text) {
    std::wstring out;
    out.reserve(text.size());
    size_t offset = 0;
    while (offset < text.size()) {
        const uint32_t cp = decodeUtf8(text, offset);
        if (cp <= 0xFFFF) {
            if (cp != 0) {
                out.push_back(static_cast<wchar_t>(cp));
            }
        } else {
            const uint32_t value = cp - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
        }
    }
    return out;
}

std::string PercentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return -1;
            };
            const int hi = hex(text[i + 1]);
            const int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string Base64Decode(std::string_view text) {
    static constexpr signed char kTable[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    std::string out;
    uint32_t value = 0;
    int bits = 0;
    for (const char ch : text) {
        const auto v = kTable[static_cast<unsigned char>(ch)];
        if (v == -1) {
            continue;
        }
        if (v == -2) {
            break;
        }
        value = (value << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return out;
}

std::vector<int> ParseArgs(std::string_view text) {
    std::vector<int> values;
    std::string current;
    auto flush = [&]() {
        if (current.empty()) {
            values.push_back(-1);
        } else {
            char* end = nullptr;
            const long value = std::strtol(current.c_str(), &end, 10);
            values.push_back(end && *end == 0 ? static_cast<int>(value) : -1);
        }
        current.clear();
    };
    for (const char ch : text) {
        if (ch == ';' || ch == ':') {
            flush();
        } else {
            current.push_back(ch);
        }
    }
    flush();
    return values;
}

int ArgAt(const std::vector<int>& args, size_t index, int fallback) {
    if (index >= args.size() || args[index] < 0) {
        return fallback;
    }
    return args[index];
}

}  // namespace

int codePointWidth(uint32_t cp) {
    if (cp == 0) {
        return 0;
    }
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) {
        return 0;
    }
    if (IsCombining(cp)) {
        return 0;
    }
    return IsWide(cp) ? 2 : 1;
}

void appendUtf16(wchar_t (&dest)[4], uint32_t cp) {
    size_t used = 0;
    while (used < 4 && dest[used] != 0) {
        ++used;
    }
    if (cp <= 0xFFFF) {
        if (used + 1 < 4) {
            dest[used++] = static_cast<wchar_t>(cp);
        }
    } else if (used + 2 < 4) {
        const uint32_t value = cp - 0x10000;
        dest[used++] = static_cast<wchar_t>(0xD800 + (value >> 10));
        dest[used++] = static_cast<wchar_t>(0xDC00 + (value & 0x3FF));
    }
    dest[used] = 0;
}

std::string encodeUtf8(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

uint32_t decodeUtf8(std::string_view text, size_t& offset) {
    if (offset >= text.size()) {
        return 0xFFFD;
    }
    const auto b0 = static_cast<unsigned char>(text[offset]);
    if (b0 < 0x80) {
        ++offset;
        return b0;
    }
    int need = 0;
    uint32_t cp = 0;
    uint32_t minimum = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        need = 1;
        cp = b0 & 0x1F;
        minimum = 0x80;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        need = 2;
        cp = b0 & 0x0F;
        minimum = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        need = 3;
        cp = b0 & 0x07;
        minimum = 0x10000;
    } else {
        ++offset;
        return 0xFFFD;
    }
    if (offset + static_cast<size_t>(need) >= text.size()) {
        ++offset;
        return 0xFFFD;
    }
    for (int i = 1; i <= need; ++i) {
        const auto b = static_cast<unsigned char>(text[offset + static_cast<size_t>(i)]);
        if ((b & 0xC0) != 0x80) {
            ++offset;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    offset += static_cast<size_t>(need) + 1;
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0xFFFD;
    }
    return cp;
}

// ---------------------------------------------------------------- construction

Terminal::Terminal(int columns, int rows, int scrollbackLimit)
    : columns_(std::max(2, columns)),
      rows_(std::max(1, rows)),
      scrollbackLimit_(std::max(0, scrollbackLimit)) {
    main_.assign(static_cast<size_t>(rows_), Line{});
    alt_.assign(static_cast<size_t>(rows_), Line{});
    for (auto& line : main_) {
        line.cells.assign(static_cast<size_t>(columns_), Cell{});
    }
    for (auto& line : alt_) {
        line.cells.assign(static_cast<size_t>(columns_), Cell{});
    }
    scrollBottom_ = rows_ - 1;
    tabStops_.assign(static_cast<size_t>(columns_), 0);
    for (int x = 0; x < columns_; x += 8) {
        tabStops_[static_cast<size_t>(x)] = 1;
    }
    reset(true);
}

void Terminal::reset(bool hard) {
    if (hard) {
        scrollback_.clear();
        scrollOffset_ = 0;
        title_.clear();
        workingDirectory_.clear();
        titleStack_.clear();
        savedCursor_ = SavedCursor{};
    }
    main_.assign(static_cast<size_t>(rows_), Line{});
    alt_.assign(static_cast<size_t>(rows_), Line{});
    for (auto& line : main_) {
        line.cells.assign(static_cast<size_t>(columns_), Cell{});
    }
    for (auto& line : alt_) {
        line.cells.assign(static_cast<size_t>(columns_), Cell{});
    }
    altActive_ = false;
    cursorX_ = 0;
    cursorY_ = 0;
    wrapPending_ = false;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    lastPrinted_ = 0;
    resetPen();
    resetModes();
    tabStops_.assign(static_cast<size_t>(columns_), 0);
    for (int x = 0; x < columns_; x += 8) {
        tabStops_[static_cast<size_t>(x)] = 1;
    }
}

void Terminal::resetPen() {
    penAttrs_ = 0;
    penFg_ = Color::DefaultColor();
    penBg_ = Color::DefaultColor();
}

void Terminal::resetModes() {
    autowrap_ = true;
    insertMode_ = false;
    originMode_ = false;
    newlineMode_ = false;
    reverseVideo_ = false;
    applicationCursorKeys_ = false;
    applicationKeypad_ = false;
    bracketedPaste_ = false;
    focusReporting_ = false;
    mouseTracking_ = 0;
    sgrMouse_ = false;
    cursorVisible_ = true;
    cursorBlink_ = true;
    cursorShape_ = CursorShape::Block;
    wrapPending_ = false;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

void Terminal::setScrollbackLimit(int lines) {
    scrollbackLimit_ = std::max(0, lines);
    while (static_cast<int>(scrollback_.size()) > scrollbackLimit_) {
        scrollback_.pop_front();
        if (scrollOffset_ > 0) {
            --scrollOffset_;
        }
    }
    setScrollOffset(scrollOffset_);
}

void Terminal::setPixelSize(int width, int height) {
    pixelWidth_ = std::max(0, width);
    pixelHeight_ = std::max(0, height);
}

// ---------------------------------------------------------------- view access

std::vector<Line>& Terminal::grid() {
    return altActive_ ? alt_ : main_;
}

const std::vector<Line>& Terminal::grid() const {
    return altActive_ ? alt_ : main_;
}

Line& Terminal::lineAt(int y) {
    return grid()[static_cast<size_t>(ClampInt(y, 0, rows_ - 1))];
}

const Line& Terminal::lineAt(int y) const {
    return grid()[static_cast<size_t>(ClampInt(y, 0, rows_ - 1))];
}

Line& Terminal::combinedLine(int absoluteRow) {
    const int sb = static_cast<int>(scrollback_.size());
    if (absoluteRow < sb) {
        return scrollback_[static_cast<size_t>(absoluteRow)];
    }
    return grid()[static_cast<size_t>(absoluteRow - sb)];
}

const Line* Terminal::combinedLinePtr(int absoluteRow) const {
    const int sb = static_cast<int>(scrollback_.size());
    if (absoluteRow < 0 || absoluteRow >= sb + rows_) {
        return nullptr;
    }
    if (absoluteRow < sb) {
        return &scrollback_[static_cast<size_t>(absoluteRow)];
    }
    return &grid()[static_cast<size_t>(absoluteRow - sb)];
}

const Cell& Terminal::viewCell(int x, int y) const {
    static const Cell blank{};
    if (x < 0 || x >= columns_ || y < 0 || y >= rows_) {
        return blank;
    }
    const int offset = altActive_ ? 0 : scrollOffset_;
    const int sb = static_cast<int>(scrollback_.size());
    const int absolute = sb - offset + y;
    const Line* line = combinedLinePtr(absolute);
    if (!line || x >= static_cast<int>(line->cells.size())) {
        return blank;
    }
    return line->cells[static_cast<size_t>(x)];
}

const Line* Terminal::viewLine(int y) const {
    if (y < 0 || y >= rows_) {
        return nullptr;
    }
    const int offset = altActive_ ? 0 : scrollOffset_;
    const int sb = static_cast<int>(scrollback_.size());
    return combinedLinePtr(sb - offset + y);
}

int Terminal::maxScrollOffset() const {
    return altActive_ ? 0 : static_cast<int>(scrollback_.size());
}

void Terminal::setScrollOffset(int offset) {
    scrollOffset_ = ClampInt(offset, 0, maxScrollOffset());
}

void Terminal::scrollBy(int lines) {
    setScrollOffset(scrollOffset_ + lines);
}

void Terminal::clearScrollback() {
    scrollback_.clear();
    scrollOffset_ = 0;
}

// ---------------------------------------------------------------- buffer ops

Cell Terminal::blankCell() const {
    Cell cell;
    cell.width = 1;
    return cell;
}

Cell Terminal::eraseCell() const {
    Cell cell;
    cell.width = 1;
    cell.bg = penBg_;
    return cell;
}

void Terminal::clearWideNeighbor(int x, int y) {
    if (x < 0 || x >= columns_ || y < 0 || y >= rows_) {
        return;
    }
    auto& cells = lineAt(y).cells;
    if (x < static_cast<int>(cells.size()) && cells[static_cast<size_t>(x)].width == 0 && x > 0) {
        cells[static_cast<size_t>(x - 1)] = eraseCell();
    }
    if (x < static_cast<int>(cells.size()) && cells[static_cast<size_t>(x)].width == 2) {
        if (x + 1 < columns_) {
            cells[static_cast<size_t>(x + 1)] = eraseCell();
        }
    }
}

void Terminal::putCell(int x, int y, const Cell& cell) {
    if (x < 0 || x >= columns_ || y < 0 || y >= rows_) {
        return;
    }
    auto& line = lineAt(y);
    if (static_cast<int>(line.cells.size()) != columns_) {
        line.cells.resize(static_cast<size_t>(columns_), Cell{});
    }
    clearWideNeighbor(x, y);
    line.cells[static_cast<size_t>(x)] = cell;
}

void Terminal::clearLine(int y) {
    if (y < 0 || y >= rows_) {
        return;
    }
    auto& line = lineAt(y);
    line.cells.assign(static_cast<size_t>(columns_), eraseCell());
}

void Terminal::eraseCells(int y, int x, int count) {
    if (y < 0 || y >= rows_ || count <= 0) {
        return;
    }
    const int start = ClampInt(x, 0, columns_ - 1);
    const int end = ClampInt(x + count, 0, columns_);
    for (int col = start; col < end; ++col) {
        clearWideNeighbor(col, y);
        lineAt(y).cells[static_cast<size_t>(col)] = eraseCell();
    }
}

void Terminal::pushScrollback(Line line) {
    if (scrollbackLimit_ <= 0) {
        return;
    }
    if (static_cast<int>(line.cells.size()) != columns_) {
        line.cells.resize(static_cast<size_t>(columns_), Cell{});
    }
    scrollback_.push_back(std::move(line));
    if (scrollOffset_ > 0) {
        ++scrollOffset_;
    }
    while (static_cast<int>(scrollback_.size()) > scrollbackLimit_) {
        scrollback_.pop_front();
        if (scrollOffset_ > 0) {
            --scrollOffset_;
        }
    }
    setScrollOffset(scrollOffset_);
}

void Terminal::scrollUp(int top, int bottom, int count) {
    top = ClampInt(top, 0, rows_ - 1);
    bottom = ClampInt(bottom, 0, rows_ - 1);
    if (top > bottom || count <= 0) {
        return;
    }
    count = std::min(count, bottom - top + 1);
    for (int i = 0; i < count; ++i) {
        if (top == 0 && bottom == rows_ - 1 && !altActive_) {
            pushScrollback(std::move(main_[0]));
        }
        for (int y = top; y < bottom; ++y) {
            std::swap(grid()[static_cast<size_t>(y)], grid()[static_cast<size_t>(y + 1)]);
        }
        clearLine(bottom);
    }
}

void Terminal::scrollDown(int top, int bottom, int count) {
    top = ClampInt(top, 0, rows_ - 1);
    bottom = ClampInt(bottom, 0, rows_ - 1);
    if (top > bottom || count <= 0) {
        return;
    }
    count = std::min(count, bottom - top + 1);
    for (int i = 0; i < count; ++i) {
        for (int y = bottom; y > top; --y) {
            grid()[static_cast<size_t>(y)] = std::move(grid()[static_cast<size_t>(y - 1)]);
        }
        clearLine(top);
    }
}

// ---------------------------------------------------------------- printing

void Terminal::printAscii(uint8_t ch) {
    printCodePoint(ch);
}

void Terminal::printCodePoint(uint32_t cp) {
    if (cp == 0) {
        return;
    }
    const int width = codePointWidth(cp);
    if (width == 0) {
        // Append combining mark to the preceding cell.
        int x = cursorX_ - 1;
        if (x < 0) {
            x = 0;
        }
        auto& cells = lineAt(cursorY_).cells;
        if (x < static_cast<int>(cells.size())) {
            appendUtf16(cells[static_cast<size_t>(x)].text, cp);
        }
        return;
    }

    if (wrapPending_ && autowrap_) {
        lineFeed();
        cursorX_ = 0;
        wrapPending_ = false;
    }

    if (width == 2) {
        if (cursorX_ >= columns_ - 1) {
            if (autowrap_) {
                lineFeed();
                cursorX_ = 0;
            } else {
                cursorX_ = std::max(0, columns_ - 2);
            }
        }
    }

    if (insertMode_) {
        auto& cells = lineAt(cursorY_).cells;
        const int shift = width;
        for (int x = columns_ - 1; x >= cursorX_ + shift; --x) {
            cells[static_cast<size_t>(x)] = cells[static_cast<size_t>(x - shift)];
        }
        for (int x = cursorX_; x < cursorX_ + shift && x < columns_; ++x) {
            cells[static_cast<size_t>(x)] = eraseCell();
        }
    }

    Cell cell;
    cell.width = static_cast<uint8_t>(width);
    cell.attrs = penAttrs_;
    cell.fg = penFg_;
    cell.bg = penBg_;
    appendUtf16(cell.text, cp);
    putCell(cursorX_, cursorY_, cell);

    if (width == 2 && cursorX_ + 1 < columns_) {
        Cell continuation = eraseCell();
        continuation.width = 0;
        continuation.attrs = penAttrs_;
        continuation.fg = penFg_;
        continuation.bg = penBg_;
        lineAt(cursorY_).cells[static_cast<size_t>(cursorX_ + 1)] = continuation;
    }

    lastPrinted_ = cp;
    cursorX_ += width;
    if (cursorX_ >= columns_) {
        cursorX_ = columns_ - 1;
        wrapPending_ = autowrap_;
    } else {
        wrapPending_ = false;
    }
}

void Terminal::lineFeed() {
    wrapPending_ = false;
    if (cursorY_ == scrollBottom_) {
        scrollUp(scrollTop_, scrollBottom_, 1);
    } else if (cursorY_ < rows_ - 1) {
        ++cursorY_;
    }
}

void Terminal::reverseIndex() {
    wrapPending_ = false;
    if (cursorY_ == scrollTop_) {
        scrollDown(scrollTop_, scrollBottom_, 1);
    } else if (cursorY_ > 0) {
        --cursorY_;
    }
}

void Terminal::carriageReturn() {
    cursorX_ = 0;
    wrapPending_ = false;
}

void Terminal::nextTabStop() {
    for (int x = cursorX_ + 1; x < columns_; ++x) {
        if (x < static_cast<int>(tabStops_.size()) && tabStops_[static_cast<size_t>(x)]) {
            cursorX_ = x;
            wrapPending_ = false;
            return;
        }
    }
    cursorX_ = columns_ - 1;
    wrapPending_ = false;
}

void Terminal::previousTabStop() {
    for (int x = cursorX_ - 1; x >= 0; --x) {
        if (x < static_cast<int>(tabStops_.size()) && tabStops_[static_cast<size_t>(x)]) {
            cursorX_ = x;
            wrapPending_ = false;
            return;
        }
    }
    cursorX_ = 0;
    wrapPending_ = false;
}

void Terminal::tabForward(int count) {
    for (int i = 0; i < count; ++i) {
        nextTabStop();
    }
}

void Terminal::tabBackward(int count) {
    for (int i = 0; i < count; ++i) {
        previousTabStop();
    }
}

void Terminal::execute(uint8_t ch) {
    switch (ch) {
    case 0x07:
        if (bellCallback_) {
            bellCallback_();
        }
        break;
    case 0x08:
        if (cursorX_ > 0) {
            --cursorX_;
        }
        wrapPending_ = false;
        break;
    case 0x09:
        nextTabStop();
        break;
    case 0x0A:
    case 0x0B:
    case 0x0C:
        lineFeed();
        if (newlineMode_) {
            carriageReturn();
        }
        break;
    case 0x0D:
        carriageReturn();
        break;
    case 0x0E:
    case 0x0F:
        break;
    case 0x18:
    case 0x1A:
        state_ = State::Ground;
        csiParams_.clear();
        csiIntermediates_.clear();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- parser feed

void Terminal::feed(std::string_view bytes) {
    size_t i = 0;
    while (i < bytes.size()) {
        const auto raw = static_cast<unsigned char>(bytes[i]);

        if (utf8Remaining_ > 0) {
            if ((raw & 0xC0) == 0x80) {
                utf8CodePoint_ = (utf8CodePoint_ << 6) | (raw & 0x3F);
                ++i;
                if (--utf8Remaining_ == 0) {
                    const uint32_t cp = utf8CodePoint_;
                    if (cp < utf8Minimum_ || cp > 0x10FFFF ||
                        (cp >= 0xD800 && cp <= 0xDFFF)) {
                        printCodePoint(0xFFFD);
                    } else {
                        printCodePoint(cp);
                    }
                }
                continue;
            }
            utf8Remaining_ = 0;
            printCodePoint(0xFFFD);
            continue;
        }

        switch (state_) {
        case State::Ground:
            if (raw == 0x1B) {
                state_ = State::Escape;
                escIntermediate_ = 0;
                ++i;
                continue;
            }
            if (raw < 0x20) {
                execute(raw);
                ++i;
                continue;
            }
            if (raw == 0x7F) {
                ++i;
                continue;
            }
            if (raw == 0x9B) {
                state_ = State::Csi;
                csiParams_.clear();
                csiIntermediates_.clear();
                ++i;
                continue;
            }
            if (raw == 0x9D) {
                state_ = State::Osc;
                osc_.clear();
                ++i;
                continue;
            }
            if (raw >= 0x80 && raw < 0xA0) {
                // Other C1 controls: ignore.
                ++i;
                continue;
            }
            if (raw < 0x80) {
                printAscii(raw);
                ++i;
                continue;
            }
            if (raw >= 0xC2 && raw <= 0xDF) {
                utf8CodePoint_ = raw & 0x1F;
                utf8Minimum_ = 0x80;
                utf8Remaining_ = 1;
            } else if (raw >= 0xE0 && raw <= 0xEF) {
                utf8CodePoint_ = raw & 0x0F;
                utf8Minimum_ = 0x800;
                utf8Remaining_ = 2;
            } else if (raw >= 0xF0 && raw <= 0xF4) {
                utf8CodePoint_ = raw & 0x07;
                utf8Minimum_ = 0x10000;
                utf8Remaining_ = 3;
            } else {
                printCodePoint(0xFFFD);
            }
            ++i;
            break;

        case State::Escape:
            if (raw == '[') {
                state_ = State::Csi;
                csiParams_.clear();
                csiIntermediates_.clear();
                ++i;
            } else if (raw == ']') {
                state_ = State::Osc;
                osc_.clear();
                ++i;
            } else if (raw == 'P' || raw == 'X' || raw == '^' || raw == '_') {
                state_ = State::Dcs;
                dcs_.clear();
                ++i;
            } else if (raw == '(' || raw == ')' || raw == '*' || raw == '+' || raw == '-' ||
                       raw == '.' || raw == '/') {
                state_ = State::Charset;
                ++i;
            } else if (raw == '#') {
                state_ = State::Hash;
                ++i;
            } else if (raw >= 0x20 && raw <= 0x2F) {
                escIntermediate_ = raw;
                state_ = State::EscapeIntermediate;
                ++i;
            } else if (raw == 0x1B) {
                ++i;
            } else if (raw < 0x20) {
                execute(raw);
                state_ = State::Ground;
                ++i;
            } else {
                dispatchEscape(raw);
                state_ = State::Ground;
                ++i;
            }
            break;

        case State::EscapeIntermediate:
            if (raw >= 0x30 && raw <= 0x7E) {
                // ESC (space) F sequences are not used by the core; ignore.
                state_ = State::Ground;
            } else if (raw == 0x1B) {
                state_ = State::Escape;
            }
            ++i;
            break;

        case State::Charset:
            state_ = State::Ground;
            ++i;
            break;

        case State::Hash:
            if (raw == '8') {
                for (auto& line : grid()) {
                    for (auto& cell : line.cells) {
                        cell = Cell{};
                        cell.text[0] = L'E';
                    }
                }
            }
            state_ = State::Ground;
            ++i;
            break;

        case State::Csi:
            if (raw >= 0x40 && raw <= 0x7E) {
                const std::string params = csiParams_;
                const std::string intermediates = csiIntermediates_;
                state_ = State::Ground;
                csiParams_.clear();
                csiIntermediates_.clear();
                dispatchCsi(params, intermediates, raw);
                ++i;
            } else if (raw >= 0x30 && raw <= 0x3F) {
                if (csiParams_.size() < 512) {
                    csiParams_.push_back(static_cast<char>(raw));
                }
                ++i;
            } else if (raw >= 0x20 && raw <= 0x2F) {
                if (csiIntermediates_.size() < 8) {
                    csiIntermediates_.push_back(static_cast<char>(raw));
                }
                ++i;
            } else if (raw == 0x1B) {
                state_ = State::Escape;
                ++i;
            } else if (raw == 0x18 || raw == 0x1A) {
                state_ = State::Ground;
                csiParams_.clear();
                csiIntermediates_.clear();
                ++i;
            } else {
                // Invalid byte: abandon the sequence.
                state_ = State::Ground;
                csiParams_.clear();
                csiIntermediates_.clear();
                ++i;
            }
            break;

        case State::Osc:
            if (raw == 0x07) {
                const std::string payload = osc_;
                state_ = State::Ground;
                osc_.clear();
                dispatchOsc(payload);
                ++i;
            } else if (raw == 0x1B) {
                state_ = State::OscEscape;
                ++i;
            } else {
                if (osc_.size() < 8192) {
                    osc_.push_back(static_cast<char>(raw));
                }
                ++i;
            }
            break;

        case State::OscEscape:
            if (raw == '\\') {
                const std::string payload = osc_;
                state_ = State::Ground;
                osc_.clear();
                dispatchOsc(payload);
            } else if (raw == 0x07) {
                const std::string payload = osc_;
                state_ = State::Ground;
                osc_.clear();
                dispatchOsc(payload);
            } else {
                state_ = State::Ground;
            }
            ++i;
            break;

        case State::Dcs:
            if (raw == 0x1B) {
                state_ = State::DcsEscape;
            } else if (raw == 0x07) {
                state_ = State::Ground;
                dcs_.clear();
            } else if (dcs_.size() < 8192) {
                dcs_.push_back(static_cast<char>(raw));
            }
            ++i;
            break;

        case State::DcsEscape:
            if (raw == '\\') {
                const std::string payload = dcs_;
                state_ = State::Ground;
                dcs_.clear();
                dispatchDcs(payload);
            } else {
                state_ = State::Dcs;
            }
            ++i;
            break;

        case State::IgnoreString:
            if (raw == 0x1B) {
                state_ = State::OscEscape;
                osc_.clear();
            }
            ++i;
            break;
        }
    }
}

// ---------------------------------------------------------------- escape dispatch

void Terminal::dispatchEscape(uint8_t final) {
    switch (final) {
    case '7':
        saveCursor();
        break;
    case '8':
        restoreCursor();
        break;
    case 'D':
        lineFeed();
        break;
    case 'E':
        carriageReturn();
        lineFeed();
        break;
    case 'M':
        reverseIndex();
        break;
    case 'H':
        if (cursorX_ >= 0 && cursorX_ < static_cast<int>(tabStops_.size())) {
            tabStops_[static_cast<size_t>(cursorX_)] = 1;
        }
        break;
    case 'c':
        reset(true);
        break;
    case '=':
        applicationKeypad_ = true;
        break;
    case '>':
        applicationKeypad_ = false;
        break;
    case '\\':
        break;
    default:
        break;
    }
}

void Terminal::dispatchDcs(std::string_view payload) {
    // DECRQSS: reply with a minimal success response for the common requests.
    if (payload.size() >= 2 && payload[0] == '$' && payload[1] == 'q') {
        const std::string_view request = payload.substr(2);
        std::string reply = "\x1bP0$r";
        if (request == "m") {
            reply = "\x1bP1$r0m\x1b\\";
        } else if (request == "r") {
            reply = "\x1bP1$r" + std::to_string(scrollTop_ + 1) + ";" +
                    std::to_string(scrollBottom_ + 1) + "r\x1b\\";
        }
        respond(reply);
    }
}

void Terminal::dispatchOsc(std::string_view payload) {
    const size_t semicolon = payload.find(';');
    const std::string command = std::string(payload.substr(0, semicolon));
    const std::string_view rest = semicolon == std::string_view::npos ? std::string_view{}
                                                                     : payload.substr(semicolon + 1);
    char* end = nullptr;
    const long code = std::strtol(command.c_str(), &end, 10);
    if (!end || *end != 0) {
        return;
    }
    switch (code) {
    case 0:
    case 2:
        onOscTitle(Utf8ToWideLocal(rest));
        break;
    case 1:
        break;
    case 7:
        onOscDirectory(rest);
        break;
    case 52:
        onOscClipboard(rest);
        break;
    default:
        break;
    }
}

void Terminal::onOscTitle(std::wstring value) {
    while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n' || value.back() == L' ')) {
        value.pop_back();
    }
    std::wstring clean;
    clean.reserve(value.size());
    for (const wchar_t ch : value) {
        if (ch >= 0x20 && ch != 0x7F) {
            clean.push_back(ch);
        }
    }
    if (clean.empty() || clean == title_) {
        return;
    }
    title_ = std::move(clean);
    if (titleCallback_) {
        titleCallback_(title_);
    }
}

void Terminal::onOscDirectory(std::string_view uri) {
    if (uri.size() < 7 || uri.substr(0, 7) != "file://") {
        return;
    }
    std::string_view rest = uri.substr(7);
    const size_t slash = rest.find('/');
    if (slash == std::string_view::npos) {
        return;
    }
    std::string path(rest.substr(slash));
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
        path[2] == ':') {
        path.erase(path.begin());
    }
    std::string decoded = PercentDecode(path);
    for (char& ch : decoded) {
        if (ch == '/') {
            ch = '\\';
        }
    }
    while (decoded.size() > 3 && (decoded.back() == '\\' || decoded.back() == '/')) {
        decoded.pop_back();
    }
    std::wstring wide = Utf8ToWideLocal(decoded);
    if (wide.empty() || wide == workingDirectory_) {
        return;
    }
    workingDirectory_ = std::move(wide);
    if (directoryCallback_) {
        directoryCallback_(workingDirectory_);
    }
}

void Terminal::onOscClipboard(std::string_view payload) {
    // OSC 52 ; selection ; base64-data
    const size_t semicolon = payload.find(';');
    if (semicolon == std::string_view::npos) {
        return;
    }
    const std::string_view data = payload.substr(semicolon + 1);
    if (data.empty() || data == "?") {
        return;
    }
    const std::string bytes = Base64Decode(data);
    if (bytes.empty()) {
        return;
    }
    const std::wstring wide = Utf8ToWideLocal(bytes);
    if (!wide.empty() && clipboardCallback_) {
        clipboardCallback_(wide);
    }
}

// ---------------------------------------------------------------- cursor

void Terminal::moveCursor(int x, int y, bool clampToOrigin) {
    cursorX_ = ClampInt(x, 0, columns_ - 1);
    if (clampToOrigin && originMode_) {
        cursorY_ = ClampInt(y, scrollTop_, scrollBottom_);
    } else {
        cursorY_ = ClampInt(y, 0, rows_ - 1);
    }
    wrapPending_ = false;
}

void Terminal::cursorHome() {
    cursorX_ = 0;
    cursorY_ = originMode_ ? scrollTop_ : 0;
    wrapPending_ = false;
}

void Terminal::saveCursor() {
    savedCursor_.x = cursorX_;
    savedCursor_.y = cursorY_;
    savedCursor_.attrs = penAttrs_;
    savedCursor_.fg = penFg_;
    savedCursor_.bg = penBg_;
    savedCursor_.origin = originMode_;
    savedCursor_.valid = true;
}

void Terminal::restoreCursor() {
    if (!savedCursor_.valid) {
        cursorX_ = 0;
        cursorY_ = 0;
        resetPen();
        return;
    }
    cursorX_ = ClampInt(savedCursor_.x, 0, columns_ - 1);
    cursorY_ = ClampInt(savedCursor_.y, 0, rows_ - 1);
    penAttrs_ = savedCursor_.attrs;
    penFg_ = savedCursor_.fg;
    penBg_ = savedCursor_.bg;
    originMode_ = savedCursor_.origin;
    wrapPending_ = false;
}

// ---------------------------------------------------------------- SGR

void Terminal::applySgr(std::string_view params) {
    if (params.empty()) {
        resetPen();
        return;
    }

    std::vector<std::string> tokens;
    {
        std::string current;
        for (const char ch : params) {
            if (ch == ';') {
                tokens.push_back(current);
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        tokens.push_back(current);
    }

    auto number = [](const std::string& text) -> int {
        if (text.empty()) {
            return 0;
        }
        return std::atoi(text.c_str());
    };

    auto applySimple = [&](int value) {
        if (value == 0) {
            resetPen();
        } else if (value == 1) {
            penAttrs_ |= AttrBold;
        } else if (value == 2) {
            penAttrs_ |= AttrDim;
        } else if (value == 3) {
            penAttrs_ |= AttrItalic;
        } else if (value == 4) {
            penAttrs_ |= AttrUnderline;
        } else if (value == 5 || value == 6) {
            penAttrs_ |= AttrBlink;
        } else if (value == 7) {
            penAttrs_ |= AttrInverse;
        } else if (value == 8) {
            penAttrs_ |= AttrInvisible;
        } else if (value == 9) {
            penAttrs_ |= AttrStrike;
        } else if (value == 21) {
            penAttrs_ |= AttrUnderline;
        } else if (value == 22) {
            penAttrs_ &= static_cast<uint16_t>(~(AttrBold | AttrDim));
        } else if (value == 23) {
            penAttrs_ &= static_cast<uint16_t>(~AttrItalic);
        } else if (value == 24) {
            penAttrs_ &= static_cast<uint16_t>(~AttrUnderline);
        } else if (value == 25) {
            penAttrs_ &= static_cast<uint16_t>(~AttrBlink);
        } else if (value == 27) {
            penAttrs_ &= static_cast<uint16_t>(~AttrInverse);
        } else if (value == 28) {
            penAttrs_ &= static_cast<uint16_t>(~AttrInvisible);
        } else if (value == 29) {
            penAttrs_ &= static_cast<uint16_t>(~AttrStrike);
        } else if (value >= 30 && value <= 37) {
            penFg_ = Color::IndexedColor(static_cast<uint8_t>(value - 30));
        } else if (value == 39) {
            penFg_ = Color::DefaultColor();
        } else if (value >= 40 && value <= 47) {
            penBg_ = Color::IndexedColor(static_cast<uint8_t>(value - 40));
        } else if (value == 49) {
            penBg_ = Color::DefaultColor();
        } else if (value >= 90 && value <= 97) {
            penFg_ = Color::IndexedColor(static_cast<uint8_t>(8 + value - 90));
        } else if (value >= 100 && value <= 107) {
            penBg_ = Color::IndexedColor(static_cast<uint8_t>(8 + value - 100));
        }
    };

    auto applyColon = [&](const std::string& token) {
        std::vector<int> parts;
        std::string current;
        for (const char ch : token) {
            if (ch == ':') {
                parts.push_back(current.empty() ? -1 : std::atoi(current.c_str()));
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        parts.push_back(current.empty() ? -1 : std::atoi(current.c_str()));
        if (parts.empty()) {
            return;
        }
        const int head = parts[0] < 0 ? 0 : parts[0];
        if (head == 4) {
            const int style = parts.size() > 1 ? parts[1] : 1;
            if (style == 0) {
                penAttrs_ &= static_cast<uint16_t>(~AttrUnderline);
            } else {
                penAttrs_ |= AttrUnderline;
            }
            return;
        }
        if (head == 38 || head == 48) {
            Color* target = head == 38 ? &penFg_ : &penBg_;
            if (parts.size() >= 3 && parts[1] == 5) {
                const int index = parts[2] < 0 ? 0 : parts[2];
                *target = Color::IndexedColor(static_cast<uint8_t>(ClampInt(index, 0, 255)));
            } else if (parts.size() >= 5 && parts[1] == 2) {
                int base = 2;
                if (parts[2] < 0) {
                    ++base;
                }
                if (static_cast<int>(parts.size()) > base + 2) {
                    *target = Color::RgbColor(
                        static_cast<uint8_t>(ClampInt(parts[static_cast<size_t>(base)], 0, 255)),
                        static_cast<uint8_t>(ClampInt(parts[static_cast<size_t>(base + 1)], 0, 255)),
                        static_cast<uint8_t>(ClampInt(parts[static_cast<size_t>(base + 2)], 0, 255)));
                }
            }
            return;
        }
        applySimple(head);
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        if (token.find(':') != std::string::npos) {
            applyColon(token);
            continue;
        }
        const int value = number(token);
        if (value == 38 || value == 48) {
            Color* target = value == 38 ? &penFg_ : &penBg_;
            if (i + 1 < tokens.size()) {
                const int mode = number(tokens[i + 1]);
                if (mode == 5 && i + 2 < tokens.size()) {
                    *target = Color::IndexedColor(
                        static_cast<uint8_t>(ClampInt(number(tokens[i + 2]), 0, 255)));
                    i += 2;
                    continue;
                }
                if (mode == 2 && i + 4 < tokens.size()) {
                    *target = Color::RgbColor(
                        static_cast<uint8_t>(ClampInt(number(tokens[i + 2]), 0, 255)),
                        static_cast<uint8_t>(ClampInt(number(tokens[i + 3]), 0, 255)),
                        static_cast<uint8_t>(ClampInt(number(tokens[i + 4]), 0, 255)));
                    i += 4;
                    continue;
                }
            }
            continue;
        }
        if (value == 58) {
            // Underline color: consume its arguments but do not store.
            if (i + 1 < tokens.size() && number(tokens[i + 1]) == 5) {
                i += 2;
            } else if (i + 1 < tokens.size() && number(tokens[i + 1]) == 2) {
                i += 4;
            }
            continue;
        }
        applySimple(value);
    }
}

// ---------------------------------------------------------------- CSI

void Terminal::dispatchCsi(std::string_view params, std::string_view intermediates, uint8_t final) {
    char prefix = 0;
    if (!params.empty()) {
        const char first = params.front();
        if (first == '?' || first == '>' || first == '=' || first == '<') {
            prefix = first;
            params.remove_prefix(1);
        }
    }
    const std::vector<int> args = ParseArgs(params);
    auto arg = [&](size_t index, int fallback) { return ArgAt(args, index, fallback); };
    const int count = std::max(1, arg(0, 1));

    if (final == 'm' && prefix == 0) {
        applySgr(params);
        return;
    }

    if (intermediates == " " && final == 'q') {
        setCursorStyleFromParams(params);
        return;
    }

    if (intermediates == "$" && final == 'p') {
        if (prefix == '?') {
            reportMode(arg(0, 0), true);
        }
        return;
    }

    if (intermediates == "!" && final == 'p') {
        softReset();
        return;
    }

    if (prefix == '?') {
        if (final == 'h' || final == 'l') {
            for (const int mode : args) {
                if (mode >= 0) {
                    setMode(mode, final == 'h', true);
                }
            }
            return;
        }
        if (final == 'n') {
            if (arg(0, 0) == 6) {
                respond("\x1b[?" + std::to_string(cursorY_ + 1) + ";" +
                        std::to_string(cursorX_ + 1) + "R");
            }
            return;
        }
        if (final == 'c') {
            deviceAttributes(true);
            return;
        }
        if (final == 'J' && arg(0, 0) == 3) {
            scrollback_.clear();
            scrollOffset_ = 0;
            return;
        }
        return;
    }

    if (prefix == '>') {
        if (final == 'c') {
            deviceAttributes(true);
        }
        return;
    }

    switch (final) {
    case '@': {  // ICH
        auto& cells = lineAt(cursorY_).cells;
        const int n = std::min(count, columns_ - cursorX_);
        for (int x = columns_ - 1; x >= cursorX_ + n; --x) {
            cells[static_cast<size_t>(x)] = cells[static_cast<size_t>(x - n)];
        }
        for (int x = cursorX_; x < cursorX_ + n; ++x) {
            cells[static_cast<size_t>(x)] = eraseCell();
        }
        wrapPending_ = false;
        break;
    }
    case 'A':  // CUU
        moveCursor(cursorX_, cursorY_ - count);
        break;
    case 'B':  // CUD
    case 'e':  // VPR
        moveCursor(cursorX_, cursorY_ + count);
        break;
    case 'C':  // CUF
    case 'a':  // HPR
        moveCursor(cursorX_ + count, cursorY_);
        break;
    case 'D':  // CUB
        moveCursor(cursorX_ - count, cursorY_);
        break;
    case 'E':  // CNL
        moveCursor(0, cursorY_ + count);
        break;
    case 'F':  // CPL
        moveCursor(0, cursorY_ - count);
        break;
    case 'G':  // CHA
    case '`':  // HPA
        moveCursor(arg(0, 1) - 1, cursorY_);
        break;
    case 'H':  // CUP
    case 'f':  // HVP
    {
        const int row = arg(0, 1) - 1;
        const int col = arg(1, 1) - 1;
        const int y = originMode_ ? scrollTop_ + row : row;
        moveCursor(col, y);
        break;
    }
    case 'I':  // CHT
        tabForward(count);
        break;
    case 'J': {  // ED
        const int mode = arg(0, 0);
        if (mode == 0) {
            eraseCells(cursorY_, cursorX_, columns_ - cursorX_);
            for (int y = cursorY_ + 1; y < rows_; ++y) {
                clearLine(y);
            }
        } else if (mode == 1) {
            for (int y = 0; y < cursorY_; ++y) {
                clearLine(y);
            }
            eraseCells(cursorY_, 0, cursorX_ + 1);
        } else if (mode == 2) {
            for (int y = 0; y < rows_; ++y) {
                clearLine(y);
            }
        } else if (mode == 3) {
            scrollback_.clear();
            scrollOffset_ = 0;
        }
        wrapPending_ = false;
        break;
    }
    case 'K': {  // EL
        const int mode = arg(0, 0);
        if (mode == 0) {
            eraseCells(cursorY_, cursorX_, columns_ - cursorX_);
        } else if (mode == 1) {
            eraseCells(cursorY_, 0, cursorX_ + 1);
        } else if (mode == 2) {
            clearLine(cursorY_);
        }
        wrapPending_ = false;
        break;
    }
    case 'L': {  // IL
        if (cursorY_ >= scrollTop_ && cursorY_ <= scrollBottom_) {
            scrollDown(cursorY_, scrollBottom_, count);
        }
        break;
    }
    case 'M': {  // DL
        if (cursorY_ >= scrollTop_ && cursorY_ <= scrollBottom_) {
            scrollUp(cursorY_, scrollBottom_, count);
        }
        break;
    }
    case 'P': {  // DCH
        auto& cells = lineAt(cursorY_).cells;
        const int n = std::min(count, columns_ - cursorX_);
        for (int x = cursorX_; x < columns_ - n; ++x) {
            cells[static_cast<size_t>(x)] = cells[static_cast<size_t>(x + n)];
        }
        for (int x = columns_ - n; x < columns_; ++x) {
            cells[static_cast<size_t>(x)] = eraseCell();
        }
        wrapPending_ = false;
        break;
    }
    case 'S':  // SU
        scrollUp(scrollTop_, scrollBottom_, count);
        break;
    case 'T':  // SD
        scrollDown(scrollTop_, scrollBottom_, count);
        break;
    case 'X':  // ECH
        eraseCells(cursorY_, cursorX_, count);
        wrapPending_ = false;
        break;
    case 'Z':  // CBT
        tabBackward(count);
        break;
    case 'b':  // REP
        if (lastPrinted_ != 0) {
            for (int i = 0; i < count; ++i) {
                printCodePoint(lastPrinted_);
            }
        }
        break;
    case 'c':  // DA
        deviceAttributes(false);
        break;
    case 'd':  // VPA
    {
        const int row = arg(0, 1) - 1;
        moveCursor(cursorX_, originMode_ ? scrollTop_ + row : row);
        break;
    }
    case 'g': {  // TBC
        const int mode = arg(0, 0);
        if (mode == 0) {
            if (cursorX_ < static_cast<int>(tabStops_.size())) {
                tabStops_[static_cast<size_t>(cursorX_)] = 0;
            }
        } else if (mode == 3) {
            std::fill(tabStops_.begin(), tabStops_.end(), 0);
        }
        break;
    }
    case 'h':
    case 'l': {
        const bool enabled = final == 'h';
        for (const int mode : args) {
            if (mode >= 0) {
                setMode(mode, enabled, false);
            }
        }
        break;
    }
    case 'n': {  // DSR
        const int mode = arg(0, 0);
        if (mode == 5) {
            respond("\x1b[0n");
        } else if (mode == 6) {
            respond("\x1b[" + std::to_string(cursorY_ + 1) + ";" +
                    std::to_string(cursorX_ + 1) + "R");
        }
        break;
    }
    case 'r': {  // DECSTBM
        int top = arg(0, 1) - 1;
        int bottom = arg(1, rows_) - 1;
        top = ClampInt(top, 0, rows_ - 1);
        bottom = ClampInt(bottom, 0, rows_ - 1);
        if (top < bottom) {
            scrollTop_ = top;
            scrollBottom_ = bottom;
            cursorHome();
        }
        break;
    }
    case 's':
        saveCursor();
        break;
    case 'u':
        restoreCursor();
        break;
    case 't':
        windowOp(arg(0, 0), arg(1, 0), arg(2, 0));
        break;
    default:
        break;
    }
}

void Terminal::setCursorStyleFromParams(std::string_view params) {
    const std::vector<int> args = ParseArgs(params);
    const int value = ArgAt(args, 0, 0);
    switch (value) {
    case 0:
        cursorBlink_ = true;
        cursorShape_ = CursorShape::Block;
        break;
    case 1:
        cursorBlink_ = true;
        cursorShape_ = CursorShape::Block;
        break;
    case 2:
        cursorBlink_ = false;
        cursorShape_ = CursorShape::Block;
        break;
    case 3:
        cursorBlink_ = true;
        cursorShape_ = CursorShape::Underline;
        break;
    case 4:
        cursorBlink_ = false;
        cursorShape_ = CursorShape::Underline;
        break;
    case 5:
        cursorBlink_ = true;
        cursorShape_ = CursorShape::Bar;
        break;
    case 6:
        cursorBlink_ = false;
        cursorShape_ = CursorShape::Bar;
        break;
    default:
        break;
    }
}

void Terminal::setMode(int mode, bool enabled, bool privateMode) {
    if (!privateMode) {
        switch (mode) {
        case 4:
            insertMode_ = enabled;
            break;
        case 20:
            newlineMode_ = enabled;
            break;
        default:
            break;
        }
        return;
    }

    switch (mode) {
    case 1:
        applicationCursorKeys_ = enabled;
        break;
    case 5:
        reverseVideo_ = enabled;
        break;
    case 6:
        originMode_ = enabled;
        cursorHome();
        break;
    case 7:
        autowrap_ = enabled;
        wrapPending_ = false;
        break;
    case 12:
        cursorBlink_ = enabled;
        break;
    case 25:
        cursorVisible_ = enabled;
        break;
    case 47:
    case 1047:
        setAltScreen(enabled, true, false);
        break;
    case 1048:
        if (enabled) {
            saveCursor();
        } else {
            restoreCursor();
        }
        break;
    case 1049:
        setAltScreen(enabled, true, true);
        break;
    case 1000:
    case 1002:
    case 1003:
        if (enabled) {
            mouseTracking_ = mode;
        } else if (mouseTracking_ == mode) {
            mouseTracking_ = 0;
        }
        break;
    case 1004:
        focusReporting_ = enabled;
        break;
    case 1005:
        break;
    case 1006:
        sgrMouse_ = enabled;
        break;
    case 1015:
        break;
    case 2004:
        bracketedPaste_ = enabled;
        break;
    default:
        break;
    }
}

void Terminal::setAltScreen(bool enable, bool clear, bool saveCursorFirst) {
    if (enable && !altActive_) {
        if (saveCursorFirst) {
            saveCursor();
        }
        altActive_ = true;
        if (clear) {
            for (auto& line : alt_) {
                line.cells.assign(static_cast<size_t>(columns_), blankCell());
            }
        }
        cursorX_ = 0;
        cursorY_ = 0;
        wrapPending_ = false;
        scrollOffset_ = 0;
    } else if (!enable && altActive_) {
        altActive_ = false;
        if (clear) {
            // xterm clears the alternate screen as it leaves.
            for (auto& line : alt_) {
                line.cells.assign(static_cast<size_t>(columns_), blankCell());
            }
        }
        scrollOffset_ = 0;
        if (saveCursorFirst) {
            restoreCursor();
        }
    }
}

void Terminal::reportMode(int mode, bool privateMode) {
    bool known = false;
    bool enabled = false;
    if (privateMode) {
        switch (mode) {
        case 1:
            known = true;
            enabled = applicationCursorKeys_;
            break;
        case 5:
            known = true;
            enabled = reverseVideo_;
            break;
        case 6:
            known = true;
            enabled = originMode_;
            break;
        case 7:
            known = true;
            enabled = autowrap_;
            break;
        case 12:
            known = true;
            enabled = cursorBlink_;
            break;
        case 25:
            known = true;
            enabled = cursorVisible_;
            break;
        case 1000:
        case 1002:
        case 1003:
            known = true;
            enabled = mouseTracking_ == mode;
            break;
        case 1004:
            known = true;
            enabled = focusReporting_;
            break;
        case 1006:
            known = true;
            enabled = sgrMouse_;
            break;
        case 2004:
            known = true;
            enabled = bracketedPaste_;
            break;
        default:
            break;
        }
        respond("\x1b[?" + std::to_string(mode) + ";" + (known ? (enabled ? "1" : "2") : "0") +
                "$y");
    } else {
        switch (mode) {
        case 4:
            known = true;
            enabled = insertMode_;
            break;
        case 20:
            known = true;
            enabled = newlineMode_;
            break;
        default:
            break;
        }
        respond("\x1b[" + std::to_string(mode) + ";" + (known ? (enabled ? "1" : "2") : "0") +
                "$y");
    }
}

void Terminal::deviceAttributes(bool secondary) {
    if (secondary) {
        respond("\x1b[>1;10;0c");
    } else {
        respond("\x1b[?62;22c");
    }
}

void Terminal::windowOp(int op, int a, int b) {
    (void)a;
    (void)b;
    switch (op) {
    case 14:
        if (pixelWidth_ > 0 && pixelHeight_ > 0) {
            respond("\x1b[4;" + std::to_string(pixelHeight_) + ";" +
                    std::to_string(pixelWidth_) + "t");
        }
        break;
    case 16:
        if (pixelWidth_ > 0 && pixelHeight_ > 0 && columns_ > 0 && rows_ > 0) {
            respond("\x1b[6;" + std::to_string(pixelHeight_ / rows_) + ";" +
                    std::to_string(pixelWidth_ / columns_) + "t");
        }
        break;
    case 18:
        respond("\x1b[8;" + std::to_string(rows_) + ";" + std::to_string(columns_) + "t");
        break;
    case 22:
        break;
    default:
        break;
    }
}

void Terminal::softReset() {
    insertMode_ = false;
    originMode_ = false;
    autowrap_ = true;
    applicationCursorKeys_ = false;
    applicationKeypad_ = false;
    cursorVisible_ = true;
    cursorBlink_ = true;
    cursorShape_ = CursorShape::Block;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    scrollOffset_ = 0;
    reverseVideo_ = false;
    newlineMode_ = false;
    resetPen();
    wrapPending_ = false;
}

void Terminal::respond(std::string text) {
    lastResponse_ = text;
    if (response_) {
        response_(std::move(text));
    }
}

// ---------------------------------------------------------------- resize

void Terminal::resize(int columns, int rows) {
    columns = std::max(2, columns);
    rows = std::max(1, rows);
    if (columns == columns_ && rows == rows_) {
        return;
    }

    auto resizeLine = [&](Line& line) {
        line.cells.resize(static_cast<size_t>(columns), Cell{});
        if (columns < columns_) {
            // Trimmed lines may leave half of a wide character at the edge.
            if (!line.cells.empty() && line.cells.back().width == 2) {
                line.cells.back() = blankCell();
            }
        }
        if (!line.cells.empty() && line.cells.front().width == 0) {
            line.cells.front() = blankCell();
        }
    };
    for (auto& line : main_) {
        resizeLine(line);
    }
    for (auto& line : alt_) {
        resizeLine(line);
    }
    for (auto& line : scrollback_) {
        resizeLine(line);
    }

    if (rows < rows_) {
        const int shift = std::max(0, cursorY_ - (rows - 1));
        if (shift > 0) {
            for (int i = 0; i < shift; ++i) {
                if (!altActive_ && !main_.empty()) {
                    pushScrollback(std::move(main_.front()));
                }
                if (!main_.empty()) {
                    main_.erase(main_.begin());
                }
            }
            cursorY_ = std::max(0, cursorY_ - shift);
        }
        if (static_cast<int>(main_.size()) > rows) {
            main_.resize(static_cast<size_t>(rows));
        }
        if (static_cast<int>(alt_.size()) > rows) {
            alt_.resize(static_cast<size_t>(rows));
        }
    }

    columns_ = columns;
    rows_ = rows;

    if (static_cast<int>(main_.size()) < rows_) {
        main_.resize(static_cast<size_t>(rows_), Line{});
    }
    if (static_cast<int>(alt_.size()) < rows_) {
        alt_.resize(static_cast<size_t>(rows_), Line{});
    }
    for (auto& line : main_) {
        line.cells.resize(static_cast<size_t>(columns_), Cell{});
    }
    for (auto& line : alt_) {
        line.cells.resize(static_cast<size_t>(columns_), Cell{});
    }

    cursorX_ = ClampInt(cursorX_, 0, columns_ - 1);
    cursorY_ = ClampInt(cursorY_, 0, rows_ - 1);
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    tabStops_.assign(static_cast<size_t>(columns_), 0);
    for (int x = 0; x < columns_; x += 8) {
        tabStops_[static_cast<size_t>(x)] = 1;
    }
    wrapPending_ = false;
    setScrollOffset(scrollOffset_);
}

}  // namespace vt
