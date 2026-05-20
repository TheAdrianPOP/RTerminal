#ifndef CONSOLE_UTILS_H
#define CONSOLE_UTILS_H

// engine_config.h must be included first — it defines all ANSI/palette constants.
// console.h no longer pulls it in transitively (that caused a circular dependency).
#include "engine_config.h" // kIntStrMaxBytes, kAnsi16*, kAnsiMaxCode, …
#include "console.h"       // RGBColor, ConsoleGlyph

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

void platform_sleep(double seconds);

void clearConsole();
void setCursorPosition(int x, int y);
void setCursorVisibility(bool visible);
void getConsoleSize(int& width, int& height);
void setupConsoleMode();
void restoreConsoleMode();

inline char* fast_itoa(int value, char* str) {
    if (value == 0) { *str++ = '0'; return str; }
    char temp[kIntStrMaxBytes];
    int p = 0;
    if (value < 0) { *str++ = '-'; value = -value; }
    
    while (value > 0) { 
        temp[p++] = (value % 10) + '0'; 
        value /= 10; 
    }
    while (p > 0) *str++ = temp[--p];
    return str;
}

inline void write_ansi_truecolor(char*& p, const RGBColor& c, bool fg) {
    *p++ = '\x1b'; *p++ = '[';
    if (fg) { *p++ = '3'; *p++ = '8'; } else { *p++ = '4'; *p++ = '8'; }
    *p++ = ';'; *p++ = '2'; *p++ = ';';
    p = fast_itoa(c.r, p); *p++ = ';';
    p = fast_itoa(c.g, p); *p++ = ';';
    p = fast_itoa(c.b, p); *p++ = 'm';
}

inline void write_ansi_256(char*& p, int index, bool fg) {
    *p++ = '\x1b'; *p++ = '[';
    if (fg) { *p++ = '3'; *p++ = '8'; } else { *p++ = '4'; *p++ = '8'; }
    *p++ = ';'; *p++ = '5'; *p++ = ';';
    p = fast_itoa(index, p); *p++ = 'm';
}

inline void write_ansi_16(char*& p, int index, bool fg) {
    *p++ = '\x1b'; *p++ = '[';
    int code;
    if (fg) code = (index < kAnsi16BrightOffset) ? (kAnsi16FgBase       + index)
                                                  : (kAnsi16BrightFgBase + index - kAnsi16BrightOffset);
    else    code = (index < kAnsi16BrightOffset) ? (kAnsi16BgBase       + index)
                                                  : (kAnsi16BrightBgBase + index - kAnsi16BrightOffset);
    p = fast_itoa(code, p); *p++ = 'm';
}

inline void write_ansi_reset(char*& p) {
    const char* s = "\x1b[0m";
    while (*s) *p++ = *s++;
}

int rgbToAnsi256(const RGBColor& c);
int rgbToAnsi16(const RGBColor& c);

#ifdef _WIN32
void flushBufferWindowsLegacy(int width, int height,
                              const CHAR_INFO* curr,
                              CHAR_INFO*       prev);
WORD ansi16ToWinAttr(int fgIndex, int bgIndex);
#endif

#endif