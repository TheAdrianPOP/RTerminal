#ifndef CONSOLE_H
#define CONSOLE_H

#include <cstdint>
#include <cstddef>

constexpr int kMaxUtf8Bytes  = 4; // max bytes in one UTF-8 codepoint
constexpr int kMaxGlyphBytes = 5; // kMaxUtf8Bytes + 1 null terminator

// Exactly 4 bytes. Aligned to 4-byte boundaries.
struct RGBColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t is_default = 1; // 1 = reset/unset, 0 = active color

    constexpr RGBColor() = default;

    constexpr RGBColor(uint8_t r_, uint8_t g_, uint8_t b_)
        : r(r_), g(g_), b(b_), is_default(0) {}

    constexpr RGBColor(int r_, int g_, int b_)
        : r(static_cast<uint8_t>(r_ < 0 ? 0 : (r_ > 255 ? 255 : r_))),
          g(static_cast<uint8_t>(g_ < 0 ? 0 : (g_ > 255 ? 255 : g_))),
          b(static_cast<uint8_t>(b_ < 0 ? 0 : (b_ > 255 ? 255 : b_))),
          is_default(static_cast<uint8_t>((r_ < 0 || g_ < 0 || b_ < 0) ? 1 : 0)) {}

    constexpr bool isReset() const { return is_default != 0; }

    constexpr bool operator==(const RGBColor& other) const {
        if (is_default && other.is_default) return true;
        return is_default == other.is_default && r == other.r && g == other.g && b == other.b;
    }
    constexpr bool operator!=(const RGBColor& other) const { return !(*this == other); }
};
static_assert(sizeof(RGBColor) == 4, "RGBColor must be exactly 4 bytes");

struct ConsoleGlyph {
    char data[kMaxGlyphBytes] = {' ', '\0', '\0', '\0', '\0'};

    constexpr ConsoleGlyph() = default;

    constexpr ConsoleGlyph(char c)
        : data{c, '\0', '\0', '\0', '\0'} {}

    constexpr ConsoleGlyph(const char* s) : data{'\0'} {
        if (!s) {
            data[0] = ' ';
            return;
        }
        size_t i = 0;
        for (; i < static_cast<size_t>(kMaxUtf8Bytes) && s[i]; ++i) {
            data[i] = s[i];
        }
        data[i] = '\0';
    }

    constexpr operator char() const { return data[0]; }

    constexpr bool operator==(const ConsoleGlyph& other) const {
        for (int i = 0; i < kMaxGlyphBytes; ++i) {
            if (data[i] != other.data[i]) return false;
            if (data[i] == '\0') break;
        }
        return true;
    }
    constexpr bool operator!=(const ConsoleGlyph& other) const { return !(*this == other); }

    constexpr bool operator==(char c) const { return data[0] == c && data[1] == '\0'; }
    constexpr bool operator!=(char c) const { return !(*this == c); }
};
static_assert(sizeof(ConsoleGlyph) == 5, "ConsoleGlyph must be 5 bytes");

// Perfectly packed to exactly 16 bytes (Power of 2).
// Fits 4 cells per 64-byte cache line with zero alignment penalties.
struct alignas(16) ConsoleCell {
    ConsoleGlyph character{}; // Offset 0 (5 bytes)
    bool is_ui = false;       // Offset 5 (1 byte)
    bool is_border = false;   // Offset 6 (1 byte)
    uint8_t _padding = 0;     // Offset 7 (1 byte) - keeps offsets aligned
    RGBColor color{};         // Offset 8 (4 bytes, 4-byte aligned)
    RGBColor bg_color{};      // Offset 12 (4 bytes, 4-byte aligned)

    constexpr ConsoleCell() = default;
    
    constexpr ConsoleCell(ConsoleGlyph c, RGBColor col, RGBColor bg = RGBColor(), bool ui = false, bool border = false)
        : character(c), is_ui(ui), is_border(border), _padding(0), color(col), bg_color(bg) {}

    constexpr bool operator==(const ConsoleCell& other) const {
        return character == other.character &&
               is_ui == other.is_ui &&
               is_border == other.is_border &&
               color == other.color &&
               bg_color == other.bg_color;
    }
    constexpr bool operator!=(const ConsoleCell& other) const { return !(*this == other); }
};
static_assert(sizeof(ConsoleCell) == 16, "ConsoleCell must be exactly 16 bytes for SIMD alignment");

#endif // CONSOLE_H