#pragma once

#ifdef _WIN32
#include <Windows.h>
#else
#include <termios.h>
#endif

#include "vector.h"
#include "str.h"

// ─────────────────────────────────────────────────────────────────────────────
// Input Events
// ─────────────────────────────────────────────────────────────────────────────
enum class InputEventType {
    None,
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    FocusGain,
    FocusLost,
    Text,
    Resize
};

struct InputEvent {
    InputEventType type = InputEventType::None;
    int            code = 0;      // Key or MouseButton index
    float          x = 0, y = 0;   // Mouse pos, wheel delta, or resize dimensions
    char           text[8] = {};   // UTF-8 text input
};

// ─────────────────────────────────────────────────────────────────────────────
// Key codes
// Printable ASCII keys use their ASCII value directly (e.g. KEY_W = 'W' = 87).
// Special keys use values above 255 to avoid collisions.
// ─────────────────────────────────────────────────────────────────────────────
enum {
    // ── Printable ASCII (use directly) ───────────────────────────────────────
    KEY_A = 'A', KEY_B = 'B', KEY_C = 'C', KEY_D = 'D', KEY_E = 'E',
    KEY_F = 'F', KEY_G = 'G', KEY_H = 'H', KEY_I = 'I', KEY_J = 'J',
    KEY_K = 'K', KEY_L = 'L', KEY_M = 'M', KEY_N = 'N', KEY_O = 'O',
    KEY_P = 'P', KEY_Q = 'Q', KEY_R = 'R', KEY_S = 'S', KEY_T = 'T',
    KEY_U = 'U', KEY_V = 'V', KEY_W = 'W', KEY_X = 'X', KEY_Y = 'Y',
    KEY_Z = 'Z',

    KEY_0 = '0', KEY_1 = '1', KEY_2 = '2', KEY_3 = '3', KEY_4 = '4',
    KEY_5 = '5', KEY_6 = '6', KEY_7 = '7', KEY_8 = '8', KEY_9 = '9',

    KEY_GRAVE     = '`', KEY_MINUS    = '-', KEY_EQUALS   = '=',
    KEY_LBRACKET  = '[', KEY_RBRACKET = ']', KEY_BACKSLASH= '\\',
    KEY_SEMICOLON = ';', KEY_APOSTROPHE = '\'',
    KEY_COMMA     = ',', KEY_PERIOD   = '.', KEY_SLASH    = '/',
    KEY_SPACE     = ' ',

    // ── Special keys (above ASCII range) ────────────────────────────────────
    KEY_ESC       = 256,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_CONTROL,
    KEY_SHIFT,
    KEY_ALT,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_TAB,        // NEW
    KEY_HOME,       // NEW
    KEY_END,        // NEW
    KEY_PGUP,       // NEW
    KEY_PGDN,       // NEW
    KEY_INSERT,     // NEW
    KEY_DELETE,     // NEW
    KEY_F1,         // NEW
    KEY_F2,         // NEW
    KEY_F3,         // NEW
    KEY_F4,         // NEW
    KEY_F5,         // NEW
    KEY_F6,         // NEW
    KEY_F7,         // NEW
    KEY_F8,         // NEW
    KEY_F9,         // NEW
    KEY_F10,        // NEW
    KEY_F11,        // NEW
    KEY_F12,        // NEW

    // ── Mouse buttons (separate namespace) ──────────────────────────────────
    MOUSE_LEFT   = 0,
    MOUSE_RIGHT  = 1,
    MOUSE_MIDDLE = 2,
    MOUSE_COUNT  = 8
};

// Large enough for all printable BMP Unicode + special keys above.
constexpr int KEY_COUNT = 65536;

// ─────────────────────────────────────────────────────────────────────────────
class InputManager {
public:
    InputManager();
    ~InputManager();

    void update();

    bool isKeyDown    (int key_code) const;
    bool isKeyPressed (int key_code) const;

    Vec2  getMousePosition()   const { return mouse_pos;          }
    Vec2  getMouseDelta()      const { return mouse_delta;        }
    float getMouseWheelDelta() const { return mouse_wheel_delta;  }

    bool isMouseButtonDown   (int btn) const {
        return (btn >= 0 && btn < MOUSE_COUNT) ? mouse_buttons[btn] : false;
    }
    bool  isMouseButtonPressed(int btn) const {
        return (btn >= 0 && btn < MOUSE_COUNT)
            ? (mouse_buttons[btn] && !prev_mouse_buttons[btn]) : false;
    }

    void setMouseLocked(bool locked);
    bool isMouseLocked() const { return mouse_locked; }

    const char* getTextInput() const { return text_input_buffer.c_str(); }

    bool pollEvent(InputEvent& out_event) {
        if (event_queue.empty()) return false;
        out_event = event_queue[0];
        event_queue.erase(event_queue.begin());
        return true;
    }

private:
#ifndef _WIN32
    int parseEscapeSequence(const char* buf, int len);
#endif
    void pushEvent(const InputEvent& ev) { event_queue.push_back(ev); }
    void processEvents(); // Sinks events into persistent state

    bool  keys     [KEY_COUNT]  = {};
    bool  prev_keys[KEY_COUNT]  = {};
    Vec2  mouse_pos;
    bool  mouse_buttons     [MOUSE_COUNT] = {};
    bool  prev_mouse_buttons[MOUSE_COUNT] = {};
    bool  has_focus = true;
    float mouse_wheel_delta = 0.f;

    float key_timers[KEY_COUNT] = {};
    PodVector<InputEvent> event_queue;
    Vec2  prev_mouse_pos;
    Vec2  mouse_delta;
    bool  mouse_locked       = false;
    double last_update_time = 0.0;

    StrBuf<512> text_input_buffer;

#ifndef _WIN32
    struct termios original_termios;
    void setTerminalRawMode();
    void restoreTerminalMode();
    void enableMouseTracking(bool enable);
#endif
};