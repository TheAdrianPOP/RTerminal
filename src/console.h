#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>
#include <vector>
#include "vector.h" // Include vector for Vec3 if needed elsewhere in console functions
#include "constants.h" // Include constants for ANSI_RESET if needed
#include <tuple> // For comparison

// Structure to hold RGB color (0-255)
// Using -1 as a sentinel for 'reset' or 'default' state.
struct RGBColor {
    int r = -1, g = -1, b = -1;

    // Check if color is default/reset
    bool isReset() const {
        return r < 0 || g < 0 || b < 0;
    }

    // Equality operator
    bool operator==(const RGBColor& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
    bool operator!=(const RGBColor& other) const {
        return !(*this == other);
    }
};

// Structure to hold the state of a single console cell
struct ConsoleCell {
    char character = ' ';
    RGBColor color; // Store color numerically
    bool is_ui = false; // Flag to identify UI elements
    bool is_border = false; // NEW: Flag to identify UI border elements

    // Overload equality operator for buffer comparison
    bool operator==(const ConsoleCell& other) const {
        return character == other.character && color == other.color && is_ui == other.is_ui && is_border == other.is_border;
    }
    bool operator!=(const ConsoleCell& other) const {
        return !(*this == other);
    }
};

// --- vec3ToAnsi256 function declaration/definition removed ---

#endif // CONSOLE_H 