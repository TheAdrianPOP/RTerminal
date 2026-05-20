#ifndef CONSOLE_H
#define CONSOLE_H

// These two constants are defined here (not via engine_config.h) to keep
// console.h free of circular dependencies — str.h includes console.h, and
// engine_config.h includes str.h, so console.h must NOT include engine_config.h.
#include <cstring>
#include <cstdint>

constexpr int kMaxUtf8Bytes  = 4; // max bytes in one UTF-8 codepoint
constexpr int kMaxGlyphBytes = 5; // kMaxUtf8Bytes + 1 null terminator

struct RGBColor {
    int16_t r = -1, g = -1, b = -1;

    RGBColor() = default;
    RGBColor(int r, int g, int b) : r((int16_t)r), g((int16_t)g), b((int16_t)b) {}

    bool isReset() const { return r < 0; }
    bool operator==(const RGBColor& other) const { 
        return r == other.r && g == other.g && b == other.b; 
    }
    bool operator!=(const RGBColor& other) const { return !(*this == other); }
};

struct ConsoleGlyph {
    char data[kMaxGlyphBytes] = {0};

    ConsoleGlyph() { data[0] = ' '; data[1] = 0; }
    ConsoleGlyph(char c) { data[0] = c; data[1] = 0; }
    ConsoleGlyph(const char* s) {
        if (!s) { data[0] = ' '; data[1] = 0; return; }
        size_t i = 0;
        for (; i < (size_t)kMaxUtf8Bytes && s[i]; ++i) data[i] = s[i];
        data[i] = 0;
    }
    operator char() const { return data[0]; }
    bool operator==(const ConsoleGlyph& other) const { return *(uint32_t*)data == *(uint32_t*)other.data; }
    bool operator!=(const ConsoleGlyph& other) const { return !(*this == other); }
    bool operator==(char c) const { return data[0] == c && data[1] == 0; }
    bool operator!=(char c) const { return !(*this == c); }
};

struct ConsoleCell {
    ConsoleGlyph character; 
    RGBColor color; 
    RGBColor bg_color;
    bool is_ui = false; 
    bool is_border = false; 

    ConsoleCell() = default;
    
    ConsoleCell(ConsoleGlyph c, RGBColor col, RGBColor bg = RGBColor(), bool ui = false, bool border = false)
        : character(c), color(col), bg_color(bg), is_ui(ui), is_border(border) {}

    bool operator==(const ConsoleCell& other) const {
        return color == other.color && bg_color == other.bg_color && character == other.character && is_ui == other.is_ui && is_border == other.is_border;
    }
    bool operator!=(const ConsoleCell& other) const { return !(*this == other); }
};

#endif