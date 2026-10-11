#include "input.h"
#include "math_utils.h"
#include "gamepad.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 199309L
    #endif
    #include <unistd.h>
    #include <fcntl.h>
    #include <cerrno>
    #include <sys/select.h>
    #include <sys/ioctl.h>
    #include <time.h>
#endif

constexpr int INPUT_READ_BUFFER_SIZE = 256;

__attribute__((noinline))
static uint32_t decode_utf8(const char* s) {
    if (!s || !*s) return 0;
    const auto* u = (const unsigned char*)s;
    if (u[0] < 0x80) return u[0];
    if ((u[0] & 0xE0) == 0xC0 && u[1])
        return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2])
        return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3])
        return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    return 0;
}

InputManager::InputManager() {
#ifndef _WIN32
    setTerminalRawMode();
    enableMouseTracking(true);
#endif
}

InputManager::~InputManager() {
#ifndef _WIN32
    enableMouseTracking(false);
    restoreTerminalMode();
#endif
}

static double get_time_seconds() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

void InputManager::processEvents() {
    for (size_t i = 0; i < event_queue.size(); ++i) {
        const auto& ev = event_queue[i];
        switch (ev.type) {
            case InputEventType::KeyDown:
            case InputEventType::KeyUp:
                if (ev.code >= 0 && ev.code < KEY_COUNT) {
                    bool down = (ev.type == InputEventType::KeyDown);
                    keys[ev.code] = down;
#ifndef _WIN32
                    key_timers[ev.code] = down ? 0.12f : 0.f;
#endif
                    uint32_t character = 0;
                    if (down) {
                        for (size_t j = i + 1; j < event_queue.size() && j < i + 3; ++j) {
                            if (event_queue[j].type == InputEventType::Text) {
                                character = decode_utf8(event_queue[j].text);
                                break;
                            }
                        }
                    }
                    if (key_callback) {
                        key_callback(down, ev.code, character, key_callback_userdata);
                    }
                }
                break;
            case InputEventType::MouseMove:
                mouse_pos = {ev.x, ev.y};
                break;
            case InputEventType::MouseButtonDown:
            case InputEventType::MouseButtonUp:
                if (ev.code >= 0 && ev.code < MOUSE_COUNT) {
                    mouse_buttons[ev.code] = (ev.type == InputEventType::MouseButtonDown);
                }
                break;
            case InputEventType::MouseWheel:
                mouse_wheel_delta += ev.x;
                break;
            case InputEventType::FocusGain:
            case InputEventType::FocusLost:
                has_focus = (ev.type == InputEventType::FocusGain);
                break;
            case InputEventType::Text:
                text_input_buffer += ev.text;
                break;
            default: break;
        }
    }
    event_queue.clear();
    event_queue_head = 0;
}

#ifdef _WIN32

namespace {
    struct KeyPair {
        uint8_t  vk;
        uint16_t key;
    };
}

static constexpr KeyPair kModifiers[] = {
    { VK_CONTROL,  KEY_CONTROL  }, { VK_SHIFT,    KEY_SHIFT   }, { VK_MENU,     KEY_ALT   },
    { VK_LCONTROL, KEY_LCONTROL }, { VK_RCONTROL, KEY_RCONTROL}, { VK_LSHIFT,   KEY_LSHIFT},
    { VK_RSHIFT,   KEY_RSHIFT   }, { VK_LMENU,    KEY_LALT    }, { VK_RMENU,    KEY_RALT  }
};

static constexpr KeyPair kVkTable[] = {
    { VK_BACK,     KEY_BACKSPACE },
    { VK_TAB,      KEY_TAB },
    { VK_ESCAPE,   KEY_ESC },
    { VK_LEFT,     KEY_LEFT },
    { VK_RIGHT,    KEY_RIGHT },
    { VK_UP,       KEY_UP },
    { VK_DOWN,     KEY_DOWN },
    { VK_DELETE,   KEY_DELETE },
    { VK_INSERT,   KEY_INSERT },
    { VK_HOME,     KEY_HOME },
    { VK_END,      KEY_END },
    { VK_PRIOR,    KEY_PGUP },
    { VK_NEXT,     KEY_PGDN },
    { VK_MULTIPLY, KEY_KP_MULTIPLY },
    { VK_ADD,      KEY_KP_PLUS },
    { VK_SUBTRACT, KEY_KP_MINUS },
    { VK_DECIMAL,  KEY_KP_PERIOD },
    { VK_DIVIDE,   KEY_KP_DIVIDE },
    { VK_CAPITAL,  KEY_CAPSLOCK },
    { VK_NUMLOCK,  KEY_NUMLOCK },
    { VK_SCROLL,   KEY_SCROLLOCK },
    { VK_SNAPSHOT, KEY_PRINT },
    { VK_PAUSE,    KEY_PAUSE }
};

static constexpr uint16_t kMouseMasks[] = {
    FROM_LEFT_1ST_BUTTON_PRESSED,
    RIGHTMOST_BUTTON_PRESSED,
    FROM_LEFT_2ND_BUTTON_PRESSED
};

// Out-of-line push helper prevents inlining vector growth and 24-byte copies across 9 call sites
__attribute__((noinline))
static void push_ev(PodVector<InputEvent>& q, InputEventType type, int code = 0, float x = 0.f, float y = 0.f) {
    InputEvent ev;
    ev.type = type;
    ev.code = code;
    ev.x = x;
    ev.y = y;
    q.push_back(ev);
}

__attribute__((noinline))
static void push_text(PodVector<InputEvent>& q, wchar_t wc) {
    InputEvent te;
    te.type = InputEventType::Text;
    if (wc < 0x80) {
        te.text[0] = static_cast<char>(wc);
    } else if (wc < 0x800) {
        te.text[0] = static_cast<char>(0xC0 | (wc >> 6));
        te.text[1] = static_cast<char>(0x80 | (wc & 0x3F));
    } else {
        te.text[0] = static_cast<char>(0xE0 | (wc >> 12));
        te.text[1] = static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
        te.text[2] = static_cast<char>(0x80 | (wc & 0x3F));
    }
    q.push_back(te);
}

__attribute__((noinline))
static bool isConsoleFocused(HWND fg) {
    if (!fg) return false;
    if (fg == GetConsoleWindow()) return true;

    char cls[64];
    if (GetClassNameA(fg, cls, sizeof(cls))) {
        return strstr(cls, "CASCADIA") || strstr(cls, "mintty") || strstr(cls, "pseudo");
    }
    return false;
}

// Pointer loop prevents compiler from unrolling 23 comparisons
__attribute__((noinline))
static int translate_vk(WORD vk, wchar_t wc, DWORD ctrl_state) {
    if (vk >= 'A' && vk <= 'Z') return vk;
    if (vk >= '0' && vk <= '9') return vk;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return KEY_KP0 + (vk - VK_NUMPAD0);
    if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + (vk - VK_F1);

    if (vk == VK_RETURN)  return (ctrl_state & ENHANCED_KEY) ? KEY_KP_ENTER : KEY_ENTER;
    if (vk == VK_SHIFT)   return (GetAsyncKeyState(VK_RSHIFT) & 0x8000) ? KEY_RSHIFT : KEY_LSHIFT;
    if (vk == VK_CONTROL) return (ctrl_state & RIGHT_CTRL_PRESSED) ? KEY_RCONTROL : KEY_LCONTROL;
    if (vk == VK_MENU)    return (ctrl_state & RIGHT_ALT_PRESSED) ? KEY_RALT : KEY_LALT;

    const KeyPair* p = kVkTable;
    const KeyPair* end = p + (sizeof(kVkTable) / sizeof(kVkTable[0]));
    while (p < end) {
        if (p->vk == vk) return p->key;
        ++p;
    }

    if (wc >= 'a' && wc <= 'z') return static_cast<int>(wc - 'a' + 'A');
    if (wc >= 32 && wc < 128) return static_cast<int>(wc);
    return -1;
}

__attribute__((noinline))
static void check_modifiers(PodVector<InputEvent>& queue, const bool* keys, bool focused) {
    const KeyPair* p = kModifiers;
    const KeyPair* end = p + (sizeof(kModifiers) / sizeof(kModifiers[0]));
    while (p < end) {
        bool down = focused && ((GetAsyncKeyState(p->vk) & 0x8000) != 0);
        if (down != keys[p->key]) {
            push_ev(queue, down ? InputEventType::KeyDown : InputEventType::KeyUp, p->key);
        }
        ++p;
    }
}

__attribute__((noinline))
static void read_console_events(HANDLE hIn, PodVector<InputEvent>& queue, const bool* prev_mouse_buttons) {
    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(hIn, &numEvents);
    if (numEvents == 0) return;

    INPUT_RECORD eventBuffer[64];
    DWORD totalRead = 0;
    constexpr DWORD kMaxPerTick = 128;

    while (numEvents > 0 && totalRead < kMaxPerTick) {
        DWORD toRead = fast_min_t(static_cast<DWORD>(64), numEvents);
        toRead = fast_min_t(toRead, kMaxPerTick - totalRead);
        DWORD eventsRead = 0;
        if (!ReadConsoleInputW(hIn, eventBuffer, toRead, &eventsRead) || eventsRead == 0) break;
        totalRead += eventsRead;
        numEvents = (numEvents > eventsRead) ? (numEvents - eventsRead) : 0;

        for (DWORD i = 0; i < eventsRead; ++i) {
            const auto& rec = eventBuffer[i];
            if (rec.EventType == FOCUS_EVENT) {
                push_ev(queue, rec.Event.FocusEvent.bSetFocus ? InputEventType::FocusGain : InputEventType::FocusLost);
            } else if (rec.EventType == KEY_EVENT) {
                const auto& ke = rec.Event.KeyEvent;
                InputEventType type = ke.bKeyDown ? InputEventType::KeyDown : InputEventType::KeyUp;
                int code = translate_vk(ke.wVirtualKeyCode, ke.uChar.UnicodeChar, ke.dwControlKeyState);

                if (code != -1) {
                    push_ev(queue, type, code);
                    int gen_mod = (code == KEY_LSHIFT   || code == KEY_RSHIFT)   ? KEY_SHIFT   :
                                  (code == KEY_LCONTROL || code == KEY_RCONTROL) ? KEY_CONTROL :
                                  (code == KEY_LALT     || code == KEY_RALT)     ? KEY_ALT     : -1;
                    if (gen_mod != -1) push_ev(queue, type, gen_mod);
                }

                wchar_t wc = ke.uChar.UnicodeChar;
                if (ke.bKeyDown && wc >= 32) {
                    push_text(queue, wc);
                }
            } else if (rec.EventType == MOUSE_EVENT) {
                const auto& me = rec.Event.MouseEvent;
                push_ev(queue, InputEventType::MouseMove, 0, static_cast<float>(me.dwMousePosition.X), static_cast<float>(me.dwMousePosition.Y));

                if (me.dwEventFlags == 0 || me.dwEventFlags == DOUBLE_CLICK) {
                    for (int b = 0; b < 3; ++b) {
                        bool down = (me.dwButtonState & kMouseMasks[b]) != 0;
                        if (down != prev_mouse_buttons[b]) {
                            push_ev(queue, down ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp, b);
                        }
                    }
                } else if (me.dwEventFlags == MOUSE_WHEELED) {
                    float delta = static_cast<short>(HIWORD(me.dwButtonState)) / static_cast<float>(WHEEL_DELTA);
                    push_ev(queue, InputEventType::MouseWheel, 0, delta);
                }
            }
        }
    }
}

static void update_mouse_lock(HWND fg, bool locked, bool focused, bool& was_locked, Vec2& delta) {
    if (locked && focused) {
        RECT rect;
        POINT current;
        if (GetWindowRect(fg, &rect) && GetCursorPos(&current)) {
            int cx = (rect.left + rect.right) / 2;
            int cy = (rect.top + rect.bottom) / 2;
            delta.u = static_cast<float>(current.x - cx);
            delta.v = static_cast<float>(current.y - cy);
            SetCursorPos(cx, cy);
        }
        if (!was_locked) {
            while (ShowCursor(FALSE) >= 0);
            was_locked = true;
        }
    } else if (was_locked) {
        while (ShowCursor(TRUE) < 0);
        was_locked = false;
    }
}

static void init_console_mode(HANDLE hIn, bool& configured) {
    DWORD dwMode = 0;
    if (GetConsoleMode(hIn, &dwMode)) {
        dwMode = (dwMode | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS)
                 & ~(ENABLE_QUICK_EDIT_MODE | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(hIn, dwMode);
        configured = true;
    }
}

void InputManager::update() {
    HWND fg = GetForegroundWindow();
    bool is_focused_win = isConsoleFocused(fg);

    globalGamepad().update(is_focused_win);

    last_update_time = get_time_seconds();

    std::memcpy(prev_keys, keys, sizeof(keys));
    std::memcpy(prev_mouse_buttons, mouse_buttons, sizeof(mouse_buttons));

    prev_mouse_pos = mouse_pos;
    mouse_delta = {0.f, 0.f};
    mouse_wheel_delta = 0.f;
    text_input_buffer.clear();
    event_queue.clear();
    event_queue_head = 0;

    update_mouse_lock(fg, mouse_locked, is_focused_win, was_locked_and_focused, mouse_delta);
    check_modifiers(event_queue, keys, is_focused_win);

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (!s_input_mode_configured && hIn && hIn != INVALID_HANDLE_VALUE) {
        init_console_mode(hIn, s_input_mode_configured);
    }

    read_console_events(hIn, event_queue, prev_mouse_buttons);
    processEvents();

    if (!mouse_locked || !is_focused_win) {
        mouse_delta.u = mouse_pos.u - prev_mouse_pos.u;
        mouse_delta.v = mouse_pos.v - prev_mouse_pos.v;
    }
}

void InputManager::setMouseLocked(bool locked) {
    mouse_locked = locked;
    if (!mouse_locked) { while (ShowCursor(TRUE) < 0); }
}

#else // ──────────────────────────────────────────────────────────────────────
// Linux / POSIX input
// ─────────────────────────────────────────────────────────────────────────────

void InputManager::enableMouseTracking(bool enable) {
    const char* seq = enable ? "\x1b[?1000h\x1b[?1002h\x1b[?1006h" : "\x1b[?1006l\x1b[?1002l\x1b[?1000l";
    if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {}
}

static int read_stdin_nonblock(char* buf, int capacity) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) return 0;
    int n = (int)read(STDIN_FILENO, buf, (size_t)capacity);
    return (n > 0) ? n : 0;
}

static int parse_int(const char* s, int len, int& i) {
    int v = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i++] - '0');
    }
    return v;
}

static bool parse_sgr_mouse(const char* seq, int len,
                            int& out_btn, int& out_x, int& out_y, bool& out_release) {
    int i = 0;
    int btn = parse_int(seq, len, i);
    if (i >= len || seq[i++] != ';') return false;
    int x = parse_int(seq, len, i);
    if (i >= len || seq[i++] != ';') return false;
    int y = parse_int(seq, len, i);
    if (i >= len || (seq[i] != 'M' && seq[i] != 'm')) return false;

    out_release = (seq[i] == 'm');
    out_btn = btn;
    out_x   = x - 1;
    out_y   = y - 1;
    return true;
}

int InputManager::parseEscapeSequence(const char* buf, int len) {
    if (len < 2) return 0;

    if (buf[1] == '[') {
        if (len < 3) return 0;

        if (buf[2] == '<') {
            for (int i = 3; i < len; ++i) {
                if (buf[i] == 'M' || buf[i] == 'm') {
                    int btn, x, y; bool release;
                    if (parse_sgr_mouse(buf + 3, i - 2, btn, x, y, release)) {
                        push_ev(event_queue, InputEventType::MouseMove, 0, (float)x, (float)y);

                        if (btn & 64) {
                            if (!release) {
                                push_ev(event_queue, InputEventType::MouseWheel, 0, (btn & 1) ? -1.f : 1.f);
                            }
                        } else if (!(btn & 32)) {
                            int b = btn & 3;
                            int slot = (b == 0) ? MOUSE_LEFT : (b == 1) ? MOUSE_MIDDLE : (b == 2) ? MOUSE_RIGHT : -1;
                            if (slot >= 0) {
                                push_ev(event_queue, release ? InputEventType::MouseButtonUp : InputEventType::MouseButtonDown, slot);
                            }
                        }
                    }
                    return i + 1;
                }
            }
            return 0;
        }

        if (buf[2] == 'I') { push_ev(event_queue, InputEventType::FocusGain); return 3; }
        if (buf[2] == 'O') { push_ev(event_queue, InputEventType::FocusLost); return 3; }

        for (int i = 2; i < len; ++i) {
            char c = buf[i];
            if (c >= 'A' && c <= 'Z') {
                int code = (c == 'A') ? KEY_UP :
                           (c == 'B') ? KEY_DOWN :
                           (c == 'C') ? KEY_RIGHT :
                           (c == 'D') ? KEY_LEFT :
                           (c == 'H') ? KEY_HOME :
                           (c == 'F') ? KEY_END : -1;

                if (code != -1) push_ev(event_queue, InputEventType::KeyDown, code);

                for (int j = 2; j < i; ++j) {
                    if (buf[j] == ';' && j + 1 < i) {
                        int idx = j + 1;
                        int mod = parse_int(buf, i, idx) - 1;
                        if (mod & 1) { push_ev(event_queue, InputEventType::KeyDown, KEY_LSHIFT); push_ev(event_queue, InputEventType::KeyDown, KEY_SHIFT); }
                        if (mod & 2) { push_ev(event_queue, InputEventType::KeyDown, KEY_LALT);   push_ev(event_queue, InputEventType::KeyDown, KEY_ALT); }
                        if (mod & 4) { push_ev(event_queue, InputEventType::KeyDown, KEY_LCONTROL); push_ev(event_queue, InputEventType::KeyDown, KEY_CONTROL); }
                        break;
                    }
                }
                return i + 1;
            }
            if (c == 'u') {
                int idx = 2;
                int key_code = parse_int(buf, i, idx);
                int event_type = 1; // 1 = press, 2 = repeat, 3 = release
                if (idx < i && buf[idx] == ';') {
                    idx++;
                    int mod = parse_int(buf, i, idx);
                    (void)mod;
                    if (idx < i && buf[idx] == ':') {
                        idx++;
                        event_type = parse_int(buf, i, idx);
                    }
                }
                int final_code = key_code;
                if (key_code >= 'a' && key_code <= 'z') final_code = key_code - 'a' + 'A';
                else if (key_code == 13)  final_code = KEY_ENTER;
                else if (key_code == 27)  final_code = KEY_ESC;
                else if (key_code == 127) final_code = KEY_BACKSPACE;
                else if (key_code == 9)   final_code = KEY_TAB;

                if (final_code >= 0 && final_code < KEY_COUNT) {
                    if (event_type == 3) {
                        push_ev(event_queue, InputEventType::KeyUp, final_code);
                        key_timers[final_code] = 0.f;
                    } else if (event_type == 1 || event_type == 2) {
                        push_ev(event_queue, InputEventType::KeyDown, final_code);
                        key_timers[final_code] = 0.f;
                    }
                }
                return i + 1;
            }
            if (c == '~') {
                int idx = 2;
                int val = parse_int(buf, i, idx);
                int code = (val == 1)  ? KEY_HOME :
                           (val == 4)  ? KEY_END :
                           (val == 5)  ? KEY_PGUP :
                           (val == 6)  ? KEY_PGDN :
                           (val >= 11 && val <= 15) ? KEY_F1 + (val - 11) :
                           (val >= 17 && val <= 21) ? KEY_F6 + (val - 17) :
                           (val == 23) ? KEY_F11 :
                           (val == 24) ? KEY_F12 : -1;

                if (code != -1) push_ev(event_queue, InputEventType::KeyDown, code);
                return i + 1;
            }
        }
        return 0;
    }

    if (buf[1] == 'O' && len >= 3) {
        char c = buf[2];
        int code = -1;
        if (c >= 'A' && c <= 'D') {
            const int dir[] = { KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT };
            code = dir[c - 'A'];
        } else if (c >= 'P' && c <= 'S') {
            code = KEY_F1 + (c - 'P');
        } else if (c >= 'p' && c <= 'y') {
            code = KEY_KP0 + (c - 'p');
        } else {
            switch (c) {
                case 'n': code = KEY_KP_PERIOD;   break;
                case 'M': code = KEY_KP_ENTER;    break;
                case 'o': code = KEY_KP_DIVIDE;   break;
                case 'j': code = KEY_KP_MULTIPLY; break;
                case 'k': code = KEY_KP_MINUS;    break;
                case 'l': code = KEY_KP_PLUS;     break;
                case 'X': code = KEY_KP_EQUALS;   break;
                default: break;
            }
        }
        if (code != -1) push_ev(event_queue, InputEventType::KeyDown, code);
        return 3;
    }

    return 1;
}

void InputManager::update() {
    globalGamepad().update(true);

    double now = get_time_seconds();
    if (last_update_time == 0) last_update_time = now;
    float dt = (float)(now - last_update_time);
    last_update_time = now;

    std::memcpy(prev_keys, keys, sizeof(keys));
    std::memcpy(prev_mouse_buttons, mouse_buttons, sizeof(mouse_buttons));

    for (int i = 0; i < KEY_COUNT; ++i) {
        if (key_timers[i] > 0.f) {
            key_timers[i] -= dt;
            if (key_timers[i] <= 0.f) {
                key_timers[i] = 0.f;
                push_ev(event_queue, InputEventType::KeyUp, i);
            }
        }
    }

    prev_mouse_pos    = mouse_pos;
    mouse_delta       = {0.f, 0.f};
    mouse_wheel_delta = 0.f;
    text_input_buffer.clear();
    event_queue.clear();
    event_queue_head  = 0;

    char buf[INPUT_READ_BUFFER_SIZE];
    int  bytes = read_stdin_nonblock(buf, sizeof(buf) - 1);
    if (bytes > 0) {
        buf[bytes] = '\0';
        int i = 0;
        while (i < bytes) {
            unsigned char c = (unsigned char)buf[i];

            if (c == 0x1b) {
                int consumed = parseEscapeSequence(buf + i, bytes - i);
                if (consumed <= 1) {
                    push_ev(event_queue, InputEventType::KeyDown, KEY_ESC);
                    i += 1;
                } else {
                    i += consumed;
                }
                continue;
            }

            if (c == 0x7f || c == 0x08) {
                push_ev(event_queue, InputEventType::KeyDown, KEY_BACKSPACE);
                InputEvent te; te.type = InputEventType::Text; te.text[0] = '\b';
                event_queue.push_back(te);
                i++; continue;
            }
            if (c == '\n' || c == '\r') {
                push_ev(event_queue, InputEventType::KeyDown, KEY_ENTER);
                i++; continue;
            }
            if (c == '\t') {
                push_ev(event_queue, InputEventType::KeyDown, KEY_TAB);
                i++; continue;
            }

            if (c >= 1 && c <= 26) {
                push_ev(event_queue, InputEventType::KeyDown, KEY_CONTROL);
                push_ev(event_queue, InputEventType::KeyDown, 'A' + c - 1);
                i++; continue;
            }

            if (c >= 32 && c < 128) {
                int code = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : (int)c;
                push_ev(event_queue, InputEventType::KeyDown, code);
                InputEvent te; te.type = InputEventType::Text; te.text[0] = (char)c;
                event_queue.push_back(te);
                i++; continue;
            }

            if (c >= 0xC0) {
                int char_len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
                char_len = fast_min_t(char_len, bytes - i);
                InputEvent te; te.type = InputEventType::Text;
                std::memcpy(te.text, buf + i, char_len);
                event_queue.push_back(te);
                i += char_len;
                continue;
            }
            i++;
        }
    }

    processEvents();

    mouse_delta.u = mouse_pos.u - prev_mouse_pos.u;
    mouse_delta.v = mouse_pos.v - prev_mouse_pos.v;
}

void InputManager::setTerminalRawMode() {
    tcgetattr(STDIN_FILENO, &original_termios);
    struct termios raw = original_termios;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(tcflag_t)(IXON | IXOFF | ICRNL | INPCK | ISTRIP);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cflag |=  (tcflag_t)(CS8);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    const char* focus_seq = "\x1b[?1004h\x1b=\x1b[>1u";
    if (write(STDOUT_FILENO, focus_seq, strlen(focus_seq)) < 0) {}
}

void InputManager::restoreTerminalMode() {
    const char* focus_seq = "\x1b[<u\x1b[?1004l\x1b>";
    if (write(STDOUT_FILENO, focus_seq, strlen(focus_seq)) < 0) {}
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

void InputManager::setMouseLocked(bool locked) {
    mouse_locked = locked;
    const char* seq = locked ? "\x1b[?25l" : "\x1b[?25h";
    if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {}
}

#endif // !_WIN32

bool InputManager::isKeyDown(int key_code) const {
    if (key_code >= 0 && key_code < KEY_COUNT) return keys[key_code];
    return false;
}

bool InputManager::isKeyPressed(int key_code) const {
    if (key_code >= 0 && key_code < KEY_COUNT)
        return keys[key_code] && !prev_keys[key_code];
    return false;
}