#include "console_utils.h"
#include "engine_config.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
    #include <windows.h>
    static HANDLE getCachedStdoutHandle() {
        static HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        return hOut;
    }
#else
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif

// Compile-time 256-byte LUT mapping 0..255 to closest 6-level cube coordinate (0..5)
// Levels: 0, 95, 135, 175, 215, 255
// Threshold midpoints: 48, 115, 155, 195, 235
struct CubePosTable {
    uint8_t pos[256];
};

static constexpr CubePosTable generate_cube_pos_table() {
    CubePosTable t{};
    for (int i = 0; i < 256; ++i) {
        if (i < 48)       t.pos[i] = 0;
        else if (i < 115) t.pos[i] = 1;
        else if (i < 155) t.pos[i] = 2;
        else if (i < 195) t.pos[i] = 3;
        else if (i < 235) t.pos[i] = 4;
        else              t.pos[i] = 5;
    }
    return t;
}

static constexpr CubePosTable kCubePos = generate_cube_pos_table();

// Constant-time mathematical mapping (Zero float math, zero loops, 256 bytes total)
int rgbToAnsi256(const RGBColor& c) {
    if (c.isReset()) return 7;

    // Grayscale ramp check
    if (c.r == c.g && c.g == c.b) {
        if (c.r < kAnsiGrayInputMin) return kAnsi16SystemEnd;
        if (c.r > kAnsiGrayInputMax) return kAnsiCubeEnd;
        return kAnsiGrayBase + (((static_cast<int>(c.r) - kAnsiGrayInputMin) * kAnsiGraySteps) / kAnsiGrayRange);
    }

    // Independent 1D lookups (separable L2 distance)
    int ir = kCubePos.pos[c.r];
    int ig = kCubePos.pos[c.g];
    int ib = kCubePos.pos[c.b];

    return kAnsi16SystemEnd + (kAnsiCubeStride_R * ir) + (kAnsiCubeStride_G * ig) + ib;
}

struct PaletteRGB { uint8_t r, g, b; };

static constexpr PaletteRGB kAnsi16Palette[16] = {
    {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0}, {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
    {128,128,128}, {255,0,0}, {0,255,0}, {255,255,0}, {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255}
};

int rgbToAnsi16(const RGBColor& c) {
    if (c.isReset()) return 7;

    int best = 0;
    int min_d = 0x7FFFFFFF;

    // 16 iterations unrolled by compiler with SIMD registers
    for (int i = 0; i < 16; ++i) {
        int dr = static_cast<int>(c.r) - kAnsi16Palette[i].r;
        int dg = static_cast<int>(c.g) - kAnsi16Palette[i].g;
        int db = static_cast<int>(c.b) - kAnsi16Palette[i].b;
        int d = dr * dr + dg * dg + db * db;
        if (d < min_d) {
            min_d = d;
            best = i;
            if (d == 0) break;
        }
    }
    return best;
}

int rgbToAnsi8(const RGBColor& c) {
    if (c.isReset()) return 7;

    int best = 0;
    int min_d = 0x7FFFFFFF;

    for (int i = 0; i < 8; ++i) {
        int dr = static_cast<int>(c.r) - kAnsi16Palette[i].r;
        int dg = static_cast<int>(c.g) - kAnsi16Palette[i].g;
        int db = static_cast<int>(c.b) - kAnsi16Palette[i].b;
        int d = dr * dr + dg * dg + db * db;
        if (d < min_d) {
            min_d = d;
            best = i;
            if (d == 0) break;
        }
    }
    return best;
}

void platform_sleep(double seconds) {
    if (seconds <= 0.0) return;
#ifdef _WIN32
    Sleep(static_cast<DWORD>(seconds * kMsPerSecond));
#else
    static constexpr double kUsPerSecond = 1000000.0;
    usleep(static_cast<useconds_t>(seconds * kUsPerSecond));
#endif
}

void getConsoleSize(int& width, int& height) {
    width  = MIN_CONSOLE_WIDTH;
    height = MIN_CONSOLE_HEIGHT;

#ifdef _WIN32
    HANDLE hOut = getCachedStdoutHandle();
    if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            width = fast_max_t((int)MIN_CONSOLE_WIDTH, (int)csbi.dwSize.X);
            height = fast_max_t((int)MIN_CONSOLE_HEIGHT, (int)csbi.dwSize.Y - 1);
        }
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        width = fast_max_t((int)MIN_CONSOLE_WIDTH, (int)ws.ws_col);
        height = fast_max_t((int)MIN_CONSOLE_HEIGHT, (int)ws.ws_row - 1);
    }
#endif
}

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

void setupConsoleMode() {
    setCursorVisibility(false);
#ifdef _WIN32
    HANDLE hOut = getCachedStdoutHandle();
    if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
}

void restoreConsoleMode() {
    setCursorVisibility(true);
    printf("%s", ANSI_RESET);
    fflush(stdout);
}

#ifdef _WIN32
    void clearConsole() {
        HANDLE hConsole = getCachedStdoutHandle();
        if (hConsole == INVALID_HANDLE_VALUE || hConsole == NULL) return;
        
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        
        DWORD written;
        static const char seq[] = "\x1b[2J\x1b[H";
        WriteFile(hConsole, seq, sizeof(seq) - 1, &written, NULL);
    }

    void setCursorPosition(int x, int y) {
        COORD coord; 
        coord.X = (SHORT)x; 
        coord.Y = (SHORT)y;
        SetConsoleCursorPosition(getCachedStdoutHandle(), coord);
    }

    void setCursorVisibility(bool visible) {
        HANDLE hConsole = getCachedStdoutHandle();
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        cursorInfo.bVisible = visible; 
        SetConsoleCursorInfo(hConsole, &cursorInfo);
    }
#else
    void clearConsole() { 
        if (write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7) < 0) {} 
    }

    void setCursorPosition(int x, int y) { 
        char buf[32];
        char* p = buf;
        write_cursor_move(p, x, y);
        if (write(STDOUT_FILENO, buf, p - buf) < 0) {}
    }

    void setCursorVisibility(bool visible) { 
        if (visible) {
            if (write(STDOUT_FILENO, "\x1b[?25h", 6) < 0) {}
        } else {
            if (write(STDOUT_FILENO, "\x1b[?25l", 6) < 0) {}
        }
    }
#endif