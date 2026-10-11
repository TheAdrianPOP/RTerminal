#ifndef CONSOLE_UTILS_H
#define CONSOLE_UTILS_H

#include "engine_config.h" 
#include "console.h"       
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#endif

// Compile-time LUT for 0-255 string representations (1,024 bytes in .rodata)
struct ByteStringEntry {
    char data[4];
    uint8_t len;
};

struct AnsiByteTable {
    ByteStringEntry entries[256];
};

inline constexpr AnsiByteTable generate_byte_lut() {
    AnsiByteTable table{};
    for (int i = 0; i < 256; ++i) {
        if (i < 10) {
            table.entries[i].data[0] = static_cast<char>('0' + i);
            table.entries[i].data[1] = '\0';
            table.entries[i].len = 1;
        } else if (i < 100) {
            table.entries[i].data[0] = static_cast<char>('0' + (i / 10));
            table.entries[i].data[1] = static_cast<char>('0' + (i % 10));
            table.entries[i].data[2] = '\0';
            table.entries[i].len = 2;
        } else {
            table.entries[i].data[0] = static_cast<char>('0' + (i / 100));
            table.entries[i].data[1] = static_cast<char>('0' + ((i / 10) % 10));
            table.entries[i].data[2] = static_cast<char>('0' + (i % 10));
            table.entries[i].data[3] = '\0';
            table.entries[i].len = 3;
        }
    }
    return table;
}

inline constexpr AnsiByteTable kByteStrTable = generate_byte_lut();

inline char* write_fast_u8(char* p, uint8_t val) {
    const auto& entry = kByteStrTable.entries[val];
    std::memcpy(p, entry.data, entry.len);
    return p + entry.len;
}

void platform_sleep(double seconds);
void clearConsole();
void setCursorPosition(int x, int y);
void setCursorVisibility(bool visible);
void getConsoleSize(int& width, int& height);
void setupConsoleMode();
void restoreConsoleMode();

// Replaces the 1,280-byte kByteStrTable entirely
static inline char* fast_itoa(int val, char* out) {
    if (val < 0) {
        *out++ = '-';
        val = -val;
    }

    if (val >= 100) {
        // GCC optimizes / 100 and / 10 to fast multiplication & shifts
        *out++ = (char)('0' + (val / 100));
        val %= 100;
        *out++ = (char)('0' + (val / 10));
        *out++ = (char)('0' + (val % 10));
    } else if (val >= 10) {
        *out++ = (char)('0' + (val / 10));
        *out++ = (char)('0' + (val % 10));
    } else {
        *out++ = (char)('0' + val);
    }
    return out;
}

inline void write_ansi_truecolor(char*& p, const RGBColor& c, bool fg) {
    std::memcpy(p, fg ? "\x1b[38;2;" : "\x1b[48;2;", 7);
    p += 7;
    p = write_fast_u8(p, c.r);
    *p++ = ';';
    p = write_fast_u8(p, c.g);
    *p++ = ';';
    p = write_fast_u8(p, c.b);
    *p++ = 'm';
}

inline void write_ansi_256(char*& p, int index, bool fg) {
    std::memcpy(p, fg ? "\x1b[38;5;" : "\x1b[48;5;", 7);
    p += 7;
    p = write_fast_u8(p, static_cast<uint8_t>(index & 0xFF));
    *p++ = 'm';
}

inline void write_ansi_16(char*& p, int index, bool fg) {
    *p++ = '\x1b'; *p++ = '[';
    int code;
    if (fg) code = (index < kAnsi16BrightOffset) ? (kAnsi16FgBase       + index)
                                                  : (kAnsi16BrightFgBase + index - kAnsi16BrightOffset);
    else    code = (index < kAnsi16BrightOffset) ? (kAnsi16BgBase       + index)
                                                  : (kAnsi16BrightBgBase + index - kAnsi16BrightOffset);
    p = write_fast_u8(p, static_cast<uint8_t>(code));
    *p++ = 'm';
}

inline void write_ansi_8(char*& p, int index, bool fg) {
    *p++ = '\x1b'; *p++ = '[';
    int base_idx = index & 7;
    int code = fg ? (30 + base_idx) : (40 + base_idx);
    p = write_fast_u8(p, static_cast<uint8_t>(code));
    *p++ = 'm';
}

inline void write_ansi_reset(char*& p) {
    std::memcpy(p, "\x1b[0m", 4);
    p += 4;
}

inline void write_cursor_move(char*& p, int x, int y) {
    *p++ = '\x1b'; *p++ = '[';
    p = fast_itoa(y + 1, p);
    *p++ = ';';
    p = fast_itoa(x + 1, p);
    *p++ = 'H';
}

int rgbToAnsi256(const RGBColor& c);
int rgbToAnsi16(const RGBColor& c);
int rgbToAnsi8(const RGBColor& c);

#endif