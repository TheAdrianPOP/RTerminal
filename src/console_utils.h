#ifndef CONSOLE_UTILS_H
#define CONSOLE_UTILS_H

#include <vector>
#include <string>
#ifdef _WIN32 // Include Windows.h only on Windows
#include <Windows.h> // Required for console handles and COORD
#endif
#include "console.h" // Required for RGBColor and ConsoleCell

// --- Numeric Update Command Structure ---
struct UpdateCommand {
    int x;
    int y;
    char character;
    RGBColor target_fg;
    RGBColor target_bg;
};

// --- Console Handling --- (Platform-specific implementations)
void clearConsole();
void setCursorPosition(int x, int y);
void setCursorVisibility(bool visible);

// --- Helper function to generate ANSI string from RGBColor ---
std::string colorToAnsiString(const RGBColor& color, bool foreground);

#endif // CONSOLE_UTILS_H 