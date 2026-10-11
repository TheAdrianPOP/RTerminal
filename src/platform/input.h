#pragma once

#ifndef _WIN32
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
    int            code = 0;       // Key or MouseButton index
    float          x = 0, y = 0;   // Mouse pos, wheel delta, or resize dimensions
    char           text[8] = {};   // UTF-8 text input
};

enum {
    // ── Printable ASCII ──────────────────────────────────────────────────────
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

    // ── Special keys (above ASCII range) ─────────────────────────────────────
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
    KEY_TAB,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,

    // ── Modifier extensions ─────────────────────────────────────────────────
    KEY_LCONTROL,
    KEY_RCONTROL,
    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LALT,
    KEY_RALT,

    // ── Keypad keys ─────────────────────────────────────────────────────────
    KEY_KP0,
    KEY_KP1,
    KEY_KP2,
    KEY_KP3,
    KEY_KP4,
    KEY_KP5,
    KEY_KP6,
    KEY_KP7,
    KEY_KP8,
    KEY_KP9,
    KEY_KP_PERIOD,
    KEY_KP_DIVIDE,
    KEY_KP_MULTIPLY,
    KEY_KP_MINUS,
    KEY_KP_PLUS,
    KEY_KP_ENTER,
    KEY_KP_EQUALS,

    // ── Additional locks/system keys ────────────────────────────────────────
    KEY_CAPSLOCK,
    KEY_NUMLOCK,
    KEY_SCROLLOCK,
    KEY_PRINT,
    KEY_PAUSE,

    // ── Mouse buttons ────────────────────────────────────────────────────────
    MOUSE_LEFT   = 0,
    MOUSE_RIGHT  = 1,
    MOUSE_MIDDLE = 2,
    MOUSE_COUNT  = 8
};

constexpr int KEY_COUNT = 512;

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
        if (event_queue_head >= event_queue.size()) return false;
        out_event = event_queue[event_queue_head++];
        return true;
    }

    void (*key_callback)(bool down, int key_code, uint32_t character, void* userdata) = nullptr;
    void* key_callback_userdata = nullptr;

private:
#ifndef _WIN32
    int parseEscapeSequence(const char* buf, int len);
#endif
    void pushEvent(const InputEvent& ev) { event_queue.push_back(ev); }
    void processEvents();

    bool  keys     [KEY_COUNT]  = {};
    bool  prev_keys[KEY_COUNT]  = {};
    Vec2  mouse_pos             = {};
    bool  mouse_buttons     [MOUSE_COUNT] = {};
    bool  prev_mouse_buttons[MOUSE_COUNT] = {};
    bool  has_focus = true;
    float mouse_wheel_delta = 0.f;

#ifndef _WIN32
    float key_timers[KEY_COUNT] = {};
#endif

    PodVector<InputEvent> event_queue;
    size_t event_queue_head     = 0;
    Vec2  prev_mouse_pos        = {};
    Vec2  mouse_delta           = {};
    bool  mouse_locked          = false;
    double last_update_time     = 0.0;

    StrBuf<512> text_input_buffer;

    bool was_locked_and_focused  = false;
    bool s_input_mode_configured = false;

#ifndef _WIN32
    struct termios original_termios = {};
    void setTerminalRawMode();
    void restoreTerminalMode();
    void enableMouseTracking(bool enable);
#endif
};