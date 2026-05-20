#include "console_utils.h"
#include "engine_config.h"
#include <cstdio>
#include <cstdlib>
#include <climits>  // INT_MAX

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif

void platform_sleep(double seconds) {
    if (seconds <= 0.0) return;
#ifdef _WIN32
    Sleep(static_cast<DWORD>(seconds * kMsPerSecond));
#else
    // usleep takes microseconds; 1 s = 1 000 000 µs
    static constexpr double kUsPerSecond = 1000000.0;
    usleep(static_cast<useconds_t>(seconds * kUsPerSecond));
#endif
}

void getConsoleSize(int& width, int& height) {
    // Default fallback dimensions for restricted/headless environments
    width  = MIN_CONSOLE_WIDTH;
    height = MIN_CONSOLE_HEIGHT;

#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
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

void setupConsoleMode() {
    setCursorVisibility(false);
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
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
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        DWORD dwMode = 0;
        GetConsoleMode(hConsole, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hConsole, dwMode);
        
        DWORD written;
        const char* seq = "\x1b[2J\x1b[H";
        WriteFile(hConsole, seq, 6, &written, NULL);
    }

    void setCursorPosition(int x, int y) {
        COORD coord; 
        coord.X = (SHORT)x; 
        coord.Y = (SHORT)y;
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
    }

    void setCursorVisibility(bool visible) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        cursorInfo.bVisible = visible; 
        SetConsoleCursorInfo(hConsole, &cursorInfo);
    }

    WORD ansi16ToWinAttr(int fg, int bg) {
        static const WORD map[] = {
            0, FOREGROUND_RED, FOREGROUND_GREEN, FOREGROUND_RED | FOREGROUND_GREEN,
            FOREGROUND_BLUE, FOREGROUND_RED | FOREGROUND_BLUE, FOREGROUND_GREEN | FOREGROUND_BLUE, 
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        };

        WORD winFg = 0;
        if (fg >= 8) { winFg |= FOREGROUND_INTENSITY; fg -= 8; }
        winFg |= map[fg & 7];

        WORD winBg = 0;
        if (bg >= 8) { winBg |= BACKGROUND_INTENSITY; bg -= 8; }
        WORD bgBase = map[bg & 7];
        winBg |= (bgBase << 4);

        return winFg | winBg;
    }

    void flushBufferWindowsLegacy(int width, int height,
                                  const CHAR_INFO* curr,
                                  CHAR_INFO*       prev) {
        if (!curr || !prev || width <= 0 || height <= 0) return;

        static HANDLE hOut = INVALID_HANDLE_VALUE;
        if (hOut == INVALID_HANDLE_VALUE) {
            hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        }

        const size_t rowBytes = (size_t)width * sizeof(CHAR_INFO);
        int dirtyTop = -1;
        int dirtyBottom = -1;

        for (int row = 0; row < height; ++row) {
            if (memcmp(curr + row * width, prev + row * width, rowBytes) != 0) {
                dirtyTop = row; break;
            }
        }

        if (dirtyTop < 0) return;

        for (int row = height - 1; row >= dirtyTop; --row) {
            if (memcmp(curr + row * width, prev + row * width, rowBytes) != 0) {
                dirtyBottom = row; break;
            }
        }

        COORD bufSize = { (SHORT)width, (SHORT)height };
        COORD bufCoord = { 0, (SHORT)dirtyTop };
        SMALL_RECT rgn = { 0, (SHORT)dirtyTop, (SHORT)(width - 1), (SHORT)dirtyBottom };

        WriteConsoleOutputW(hOut, curr, bufSize, bufCoord, &rgn);

        const int dirtyRows = dirtyBottom - dirtyTop + 1;
        const size_t bandBytes = (size_t)dirtyRows * rowBytes;
        memcpy(prev + dirtyTop * width, curr + dirtyTop * width, bandBytes);
    }

#else
    void clearConsole() { 
        if (write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7) < 0) {} 
    }

    void setCursorPosition(int x, int y) { 
        char buf[32];
        char* p = buf;
        *p++ = '\x1b'; *p++ = '[';
        p = fast_itoa(y + 1, p);
        *p++ = ';';
        p = fast_itoa(x + 1, p);
        *p++ = 'H';
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

int rgbToAnsi256(const RGBColor& c) {
    if (c.r == c.g && c.g == c.b) {
        if (c.r < kAnsiGrayInputMin) return kAnsi16SystemEnd;
        if (c.r > kAnsiGrayInputMax) return kAnsiCubeEnd;
        return kAnsiGrayBase + (((c.r - kAnsiGrayInputMin) * kAnsiGraySteps) / kAnsiGrayRange);
    }

    static const int levels[6] = {0, 95, 135, 175, 215, 255};
    
    auto findCubePos = [&](int val) -> int {
        if (val < 48) return 0;
        if (val < 115) return 1; 
        if (val < 155) return 2;
        if (val < 195) return 3;
        if (val < 235) return 4;
        return 5;
    };
    
    int ir0 = findCubePos(c.r);
    int ig0 = findCubePos(c.g);
    int ib0 = findCubePos(c.b);
    
    int best_idx = kAnsi16SystemEnd + (kAnsiCubeStride_R * ir0) + (kAnsiCubeStride_G * ig0) + ib0;
    float min_dist = kAnsiInitMinDist;
    
    for (int dr = 0; dr <= 1 && ir0 + dr <= 5; ++dr) {
        for (int dg = 0; dg <= 1 && ig0 + dg <= 5; ++dg) {
            for (int db = 0; db <= 1 && ib0 + db <= 5; ++db) {
                int corner_r = levels[ir0 + dr];
                int corner_g = levels[ig0 + dg];
                int corner_b = levels[ib0 + db];
                
                float dist_r = (float)(c.r - corner_r);
                float dist_g = (float)(c.g - corner_g);
                float dist_b = (float)(c.b - corner_b);
                float dist = dist_r * dist_r + dist_g * dist_g + dist_b * dist_b;
                
                if (dist < min_dist) {
                    min_dist = dist;
                    best_idx = kAnsi16SystemEnd + (kAnsiCubeStride_R * (ir0 + dr))
                                                + (kAnsiCubeStride_G * (ig0 + dg))
                                                + (ib0 + db);
                }
            }
        }
    }
    
    return best_idx;
}

int rgbToAnsi16(const RGBColor& c) {
    if (c.r < 0) return 7; 

    static const struct { int r, g, b; } palette[16] = {
        {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0}, {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
        {128,128,128}, {255,0,0}, {0,255,0}, {255,255,0}, {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255}
    };

    int bestIdx = 0;
    int minDist = INT_MAX;

    for (int i = 0; i < kAnsi16Count; ++i) {
        int dr = c.r - palette[i].r; 
        int dg = c.g - palette[i].g; 
        int db = c.b - palette[i].b;
        int dist = dr*dr + dg*dg + db*db;
        if (dist < minDist) { 
            minDist = dist; 
            bestIdx = i; 
            if (minDist == 0) break; 
        }
    }
    return bestIdx;
}