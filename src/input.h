#ifndef INPUT_H
#define INPUT_H

#ifdef _WIN32 // Include Windows.h only on Windows
#include <Windows.h> // Required for GetAsyncKeyState and Virtual Key Codes
#else // Include necessary headers for Linux/Unix input
#include <termios.h> // Required for termios struct definition
#endif

#include "vector.h" // Required for Vec3

// Define a consistent set of internal key codes
enum {
    // Alphanumeric and common symbols (using ASCII values for simplicity where they don't conflict)
    KEY_A = 'A', KEY_B = 'B', KEY_C = 'C', KEY_D = 'D', KEY_E = 'E', KEY_F = 'F', KEY_G = 'G',
    KEY_H = 'H', KEY_I = 'I', KEY_J = 'J', KEY_K = 'K', KEY_L = 'L', KEY_M = 'M', KEY_N = 'N',
    KEY_O = 'O', KEY_P = 'P', KEY_Q = 'Q', KEY_R = 'R', KEY_S = 'S', KEY_T = 'T', KEY_U = 'U',
    KEY_V = 'V', KEY_W = 'W', KEY_X = 'X', KEY_Y = 'Y', KEY_Z = 'Z',

    KEY_0 = '0', KEY_1 = '1', KEY_2 = '2', KEY_3 = '3', KEY_4 = '4',
    KEY_5 = '5', KEY_6 = '6', KEY_7 = '7', KEY_8 = '8', KEY_9 = '9',

    KEY_SPACE = ' ',
    KEY_ESC = 256, // Use values outside ASCII range for special keys
    // Removed F-key definitions
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_CONTROL, // Modifier keys might need different handling or might not have distinct press events in raw mode
    KEY_SHIFT,
    KEY_ALT,

    // Add other keys as needed
};

const int KEY_COUNT = 512; // Keep a reasonable size

class InputManager {
public:
    InputManager();
    ~InputManager(); // Destructor to restore terminal settings

    // Update function to read input state each frame
    void update();

    // Query methods for input state
    bool isKeyDown(int key_code) const;
    bool isKeyPressed(int key_code) const; // True only on the frame the key is pressed

    // Query methods for specific game actions/toggles
    bool isPauseToggled() const;
    bool isUIToggled() const;
    bool isRenderModeToggled() const;
    bool isNormalsTogglePressed() const; // Check if the Normals Visualization toggle key was just pressed
    bool isGpuTogglePressed() const; // Toggle GPU (Vulkan) rendering on/off

    Vec3 getMovementDirection(const Vec3& camera_dir, const Vec3& camera_right) const;
    void getCameraRotationDelta(double& yaw_delta, double& pitch_delta, double rotation_speed_degrees_per_sec, double deltaTime) const;

private:
    bool keys[KEY_COUNT]; // Current state of keys
    bool prev_keys[KEY_COUNT]; // State of keys in the previous frame

#ifndef _WIN32 // Linux-specific terminal settings
    struct termios original_termios;
    void setTerminalRawMode();
    void restoreTerminalMode();
#endif

    // Toggle press state (true only on the frame the key transitions from up to down)
    bool esc_pressed_this_frame = false;
    bool ui_toggle_pressed_this_frame = false; // Use a descriptive name
    bool render_mode_toggle_pressed_this_frame = false; // Use a descriptive name
    bool normals_toggle_pressed_this_frame = false; // Use a descriptive name
    bool gpu_toggle_pressed_this_frame = false; // Toggle Vulkan GPU backend
};

#endif // INPUT_MANAGER_H
