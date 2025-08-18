#include "input.h"
#include "constants.h" // For PLAYER_CAMERA_ROTATION_SPEED

#include <cmath> // For std::clamp

#ifndef _WIN32 // Include necessary headers for Linux/Unix input
#include <unistd.h> // For read, STDIN_FILENO
#include <cstring> // For memcpy, strcmp
#include <fcntl.h> // For fcntl
#include <cctype> // For toupper, isprint
#include <iostream> // For std::cerr
#include <cstdio> // For EOF
#endif

InputManager::InputManager() {
    // Initialize key states to false
    for (int i = 0; i < KEY_COUNT; ++i) {
        keys[i] = false;
        prev_keys[i] = false;
    }

#ifndef _WIN32 // Set terminal to raw mode on Linux
    setTerminalRawMode();
#endif
}

InputManager::~InputManager() {
#ifndef _WIN32 // Restore terminal mode on Linux
    restoreTerminalMode();
#endif
}

void InputManager::update() {
    // Copy current state to previous state
    for (int i = 0; i < KEY_COUNT; ++i) {
        prev_keys[i] = keys[i];
    }

    // Clear the current state for keys. We will set them to true only if
    // a press event is detected in this update.
    for (int i = 0; i < KEY_COUNT; ++i) {
        keys[i] = false;
    }

    // Reset pressed-this-frame flags
    esc_pressed_this_frame = false;
    ui_toggle_pressed_this_frame = false; // Use descriptive name
    render_mode_toggle_pressed_this_frame = false; // Use descriptive name
    normals_toggle_pressed_this_frame = false; // Use descriptive name
    gpu_toggle_pressed_this_frame = false; // Vulkan GPU toggle


#ifdef _WIN32 // Windows Implementation
    // Read current key state using GetAsyncKeyState for specific keys
    // Handle alphanumeric and other directly mapped keys
    for (int i = 'A'; i <= 'Z'; ++i) {
        if (GetAsyncKeyState(i) & 0x8000) keys[i] = true;
    }
     for (int i = '0'; i <= '9'; ++i) {
        if (GetAsyncKeyState(i) & 0x8000) keys[i] = true;
    }
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) keys[KEY_SPACE] = true;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) keys[KEY_ESC] = true;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys[KEY_CONTROL] = true; // VK_CONTROL for either left or right control
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys[KEY_SHIFT] = true;   // VK_SHIFT for either left or right shift
    if (GetAsyncKeyState(VK_MENU) & 0x8000) keys[KEY_ALT] = true;     // VK_MENU for either left or right alt

    // Handle Arrow Keys explicitly
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) keys[KEY_LEFT] = true;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) keys[KEY_RIGHT] = true;
    if (GetAsyncKeyState(VK_UP) & 0x8000) keys[KEY_UP] = true;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) keys[KEY_DOWN] = true;


#else // Linux/Unix Implementation
    // Read available input in non-blocking way
    char buf[64]; // Buffer to read input, can handle escape sequences
    int bytes_read = 0;

    // Set stdin to non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    // Read all available input in a single read call per update
    bytes_read = read(STDIN_FILENO, buf, sizeof(buf) - 1);

    if (bytes_read > 0) {
        buf[bytes_read] = '\0'; // Null-terminate the buffer

        // Process the input buffer
        for (int i = 0; i < bytes_read; ++i) {
            // Handle simple printable characters (a-z, A-Z, 0-9, symbols)
            if (std::isprint(buf[i])) {
                keys[static_cast<int>(toupper(buf[i]))] = true; // Use uppercase ASCII value as key code
            } else if (buf[i] == '\x1b') { // Start of an escape sequence
                // This is a simplified state machine for escape sequences.
                // A full-fledged parser would be more complex and stateful across update calls.
                char seq[64]; // Buffer for escape sequence
                seq[0] = buf[i];
                int seq_len = 1;
                // Attempt to read the rest of the sequence from the *already read buffer*
                while (seq_len < sizeof(seq) - 1 && i + seq_len < bytes_read) {
                     seq[seq_len] = buf[i + seq_len];
                     seq[seq_len + 1] = '\0'; // Null-terminate as we go
                     seq_len++;

                     // Check for common sequence endings or known sequences
                     if (seq[seq_len - 1] >= 'A' && seq[seq_len - 1] <= 'Z') break; // CSI sequences often end with a letter
                     if (seq[seq_len - 1] == '~') break; // Some sequences end with ~
                     // Add other checks for sequence endings if needed
                }

                // Process the captured sequence
                if (seq_len > 1) {
                    if (strcmp(seq, "\x1b[A") == 0) keys[KEY_UP] = true;
                    else if (strcmp(seq, "\x1b[B") == 0) keys[KEY_DOWN] = true;
                    else if (strcmp(seq, "\x1b[C") == 0) keys[KEY_RIGHT] = true;
                    else if (strcmp(seq, "\x1b[D") == 0) keys[KEY_LEFT] = true;
                     // Removed handling for F-keys, Page Up/Down, Home/End, Insert/Delete sequences
                } else {
                    // If only '\x1b' was read, it's likely the Escape key
                    keys[KEY_ESC] = true;
                }
                i += seq_len - 1; // Consume the characters of the sequence
            }
             // Removed handling for Tab, Ctrl+C, Enter, Backspace control characters
        }
    }

    // Restore stdin to blocking
    fcntl(STDIN_FILENO, F_SETFL, flags);

#endif // _WIN32

    // Update toggle pressed states (true only on the frame key goes from up to down)
    // Use number keys for toggles
    esc_pressed_this_frame = isKeyPressed(KEY_ESC); // Pause remains on ESC
    ui_toggle_pressed_this_frame = isKeyPressed(KEY_1); // UI toggle on '1'
    render_mode_toggle_pressed_this_frame = isKeyPressed(KEY_2); // Render mode on '2'
    normals_toggle_pressed_this_frame = isKeyPressed(KEY_3); // Normals toggle on '3'
    gpu_toggle_pressed_this_frame = isKeyPressed(KEY_4); // GPU (Vulkan) toggle on '4'
}

#ifndef _WIN32 // Linux-specific terminal mode functions
void InputManager::setTerminalRawMode() {
    struct termios new_termios;
    // Get the current terminal settings
    tcgetattr(STDIN_FILENO, &original_termios);
    // Copy the settings to modify
    new_termios = original_termios;

    // Set terminal to raw mode
    // ICANON: Disable canonical mode (line buffering)
    // ECHO: Disable echoing input characters
    new_termios.c_lflag &= ~(ICANON | ECHO);

    // ISIG: Disable signal generation from control characters (like Ctrl+C)
    // IXON, IXOFF: Disable software flow control (Ctrl+S, Ctrl+Q)
    new_termios.c_iflag &= ~(IXON | IXOFF | ISIG);

    // OPOST: Disable output processing (like converting newline to CR+LF)
    new_termios.c_oflag &= ~(OPOST);

    // VMIN = 0, VTIME = 0: Set read to be non-blocking (return immediately if no input)
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 1; // Set a small timeout to avoid busy-waiting in read

    // Apply the new settings
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

void InputManager::restoreTerminalMode() {
    // Restore the original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
}
#endif // _WIN32


bool InputManager::isKeyDown(int key_code) const {
    // Check if key_code is within the valid range
    if (key_code >= 0 && key_code < KEY_COUNT) {
        return keys[key_code];
    }
#ifdef _WIN32
    // For Windows, allow checking by char value as well for backward compatibility
    // with the original code's use of 'W', 'A', 'S', 'D'.
    if (key_code >= 'A' && key_code <= 'Z') {
        return keys[key_code]; // Assumes uppercase ASCII
    }
#endif
    return false;
}

bool InputManager::isKeyPressed(int key_code) const {
     // Check if key_code is within the valid range
     if (key_code >= 0 && key_code < KEY_COUNT) {
        return keys[key_code] && !prev_keys[key_code];
    }
#ifdef _WIN32
    // For Windows, allow checking by char value as well
     if (key_code >= 'A' && key_code <= 'Z') {
        return keys[key_code] && !prev_keys[key_code]; // Assumes uppercase ASCII
    }
#endif
    return false;
}


bool InputManager::isPauseToggled() const {
    return esc_pressed_this_frame;
}

bool InputManager::isUIToggled() const {
    return ui_toggle_pressed_this_frame;
}

bool InputManager::isRenderModeToggled() const {
    return render_mode_toggle_pressed_this_frame;
}

bool InputManager::isNormalsTogglePressed() const {
     return normals_toggle_pressed_this_frame;
}

bool InputManager::isGpuTogglePressed() const {
     return gpu_toggle_pressed_this_frame;
}


Vec3 InputManager::getMovementDirection(const Vec3& camera_dir, const Vec3& camera_right) const {
    Vec3 desired_move_direction(0.0, 0.0, 0.0);
    Vec3 forward_move = camera_dir;
    forward_move.y = 0; // Project movement onto horizontal plane
    forward_move = forward_move.normalize();

    Vec3 right_move = camera_right; // Camera right vector is already horizontal

    // Use platform-agnostic KEY_ definitions
    if (isKeyDown(KEY_W)) desired_move_direction = desired_move_direction + forward_move;
    if (isKeyDown(KEY_S)) desired_move_direction = desired_move_direction - forward_move;
    if (isKeyDown(KEY_A)) desired_move_direction = desired_move_direction - right_move;
    if (isKeyDown(KEY_D)) desired_move_direction = desired_move_direction + right_move;

    // Note: Normalization might be needed by the caller depending on how it\'s used (e.g., for direction vs velocity)
    return desired_move_direction;
}

void InputManager::getCameraRotationDelta(double& yaw_delta, double& pitch_delta, double rotation_speed_degrees_per_sec, double deltaTime) const {
    const double rotation_amount_degrees = rotation_speed_degrees_per_sec * deltaTime;

    yaw_delta = 0.0;
    pitch_delta = 0.0;

    // Use platform-agnostic KEY_ definitions
    if (isKeyDown(KEY_LEFT)) yaw_delta -= rotation_amount_degrees;
    if (isKeyDown(KEY_RIGHT)) yaw_delta += rotation_amount_degrees;
    if (isKeyDown(KEY_UP)) pitch_delta += rotation_amount_degrees;
    if (isKeyDown(KEY_DOWN)) pitch_delta -= rotation_amount_degrees;

    // Pitch clamping is done in main or the player controller
}
