#include "console_utils.h"
#include <iostream>
#include <string>
#include <sstream>

#ifndef _WIN32 // Include necessary headers for Linux/Unix
#include <cstdio> // For stdin, stdout, stderr
#include <termios.h> // For terminal control
#include <unistd.h> // For STDIN_FILENO, STDOUT_FILENO
#include <sys/ioctl.h> // For ioctl and TIOCGWINSZ
#endif

// --- Console Handling --- (Platform-specific implementations here)

#ifdef _WIN32 // Windows Implementation
void clearConsole() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE); COORD coordScreen = { 0, 0 }; DWORD cCharsWritten; CONSOLE_SCREEN_BUFFER_INFO csbi; DWORD dwConSize;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return; 
    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
    if (!FillConsoleOutputCharacter(hConsole, (TCHAR)' ', dwConSize, coordScreen, &cCharsWritten)) return;
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return;
    if (!FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen, &cCharsWritten)) return;
    SetConsoleCursorPosition(hConsole, coordScreen);
}

void setCursorPosition(int x, int y) {
    COORD coord; coord.X = static_cast<SHORT>(x); coord.Y = static_cast<SHORT>(y);
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void setCursorVisibility(bool visible) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = visible; // Set visibility based on parameter
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}

#else // Linux/Unix Implementation
void clearConsole() {
    // Use ANSI escape code to clear the screen and move cursor to top-left
    std::cout << "\\x1b[2J\\x1b[H" << std::flush;
}

void setCursorPosition(int x, int y) {
    // Use ANSI escape code to set cursor position (1-based for ANSI)
    // We assume x and y are 0-based, so add 1.
    std::cout << "\\x1b[" << y + 1 << ";" << x + 1 << "H" << std::flush;
}

void setCursorVisibility(bool visible) {
    // Use ANSI escape codes to hide/show cursor
    if (visible) {
        std::cout << "\\x1b[?25h" << std::flush; // Show cursor
    } else {
        std::cout << "\\x1b[?25l" << std::flush; // Hide cursor
    }
}
#endif // _WIN32

// --- Helper function to generate ANSI string from RGBColor ---
std::string colorToAnsiString(const RGBColor& color, bool foreground) {
    if (color.isReset()) {
        return foreground ? "\x1b[39m" : "\x1b[49m"; // Default foreground/background
    } else {
        return (foreground ? "\x1b[38;2;" : "\x1b[48;2;") +
               std::to_string(color.r) + ";" +
               std::to_string(color.g) + ";" +
               std::to_string(color.b) + "m";
    }
}
