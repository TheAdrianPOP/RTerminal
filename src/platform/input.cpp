// src/engine/platform/input.cpp
#include "input.h"
#include "math_utils.h"
#include "gamepad.h"
#include <cstdio>
#include <cwctype>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 199309L
    #endif
    #include <unistd.h>
    #include <cstring>
    #include <fcntl.h>
    #include <cctype>
    #include <cerrno>
    #include <sys/select.h>   // select() for non-blocking peek
    #include <sys/ioctl.h>
    #include <time.h>
#endif

constexpr int INPUT_READ_BUFFER_SIZE = 256;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
InputManager::InputManager() {
    for (int i = 0; i < KEY_COUNT; ++i) { keys[i] = false; prev_keys[i] = false; }
    mouse_pos = {0.f, 0.f};
    prev_mouse_pos = {0.f, 0.f};
    mouse_delta = {0.f, 0.f};
    for (int i = 0; i < MOUSE_COUNT; ++i) {
        mouse_buttons[i] = false;
        prev_mouse_buttons[i] = false;
    }
    mouse_wheel_delta = 0.f;
    mouse_locked = false;
    has_focus = true;
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

// ─────────────────────────────────────────────────────────────────────────────
// processEvents()
// ─────────────────────────────────────────────────────────────────────────────
static double get_time_seconds() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

void InputManager::processEvents() {
    for (size_t i = 0; i < event_queue.size(); ++i) {
        const auto& ev = event_queue[i];
        switch (ev.type) {
            case InputEventType::KeyDown:
                if (ev.code >= 0 && ev.code < KEY_COUNT) {
                    keys[ev.code] = true;
#ifndef _WIN32
                    key_timers[ev.code] = 0.12f; // 120ms buffer for terminal repeats
#endif
                }
                break;
            case InputEventType::KeyUp:
                if (ev.code >= 0 && ev.code < KEY_COUNT) {
                    keys[ev.code] = false;
#ifndef _WIN32
                    key_timers[ev.code] = 0.f;
#endif
                }
                break;
            case InputEventType::MouseMove:
                mouse_pos.x = ev.x;
                mouse_pos.y = ev.y;
                break;
            case InputEventType::MouseButtonDown:
                if (ev.code >= 0 && ev.code < MOUSE_COUNT) mouse_buttons[ev.code] = true;
                break;
            case InputEventType::MouseButtonUp:
                if (ev.code >= 0 && ev.code < MOUSE_COUNT) mouse_buttons[ev.code] = false;
                break;
            case InputEventType::MouseWheel:
                mouse_wheel_delta += ev.x;
                break;
            case InputEventType::FocusGain:
                has_focus = true;
                break;
            case InputEventType::FocusLost:
                has_focus = false;
                break;
            case InputEventType::Text:
                text_input_buffer += ev.text;
                break;
            default: break;
        }
    }
    event_queue.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows update 
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32

static void AppendWCharUTF8(StrBuf<512>& buf, wchar_t w) {
    if (!w) return;
    char tmp[8];
    int len = WideCharToMultiByte(CP_UTF8, 0, &w, 1, tmp, sizeof(tmp), nullptr, nullptr);
    for (int i = 0; i < len; ++i) buf += tmp[i];
}

void InputManager::update() {
    static bool was_locked_and_focused = false;
    HWND fg = GetForegroundWindow();
    HWND console = GetConsoleWindow();
    bool is_focused_win = (fg == console);

    if (!is_focused_win && fg != NULL) {
        char className[256];
        if (GetClassNameA(fg, className, sizeof(className))) {
            if (strstr(className, "CASCADIA_HOSTING_WINDOW_CLASS") ||
                strstr(className, "mintty") ||
                strstr(className, "pseudo")) {
                is_focused_win = true;
            }
        }
    }

    globalGamepad().update(is_focused_win);

    double now = get_time_seconds();
    if (last_update_time == 0) last_update_time = now;
    float dt = (float)(now - last_update_time);
    last_update_time = now;

    for (int i = 0; i < KEY_COUNT; ++i) prev_keys[i] = keys[i];
    for (int i = 0; i < MOUSE_COUNT; ++i) prev_mouse_buttons[i] = mouse_buttons[i];

#ifndef _WIN32
    // Watchdog: release keys that haven't been refreshed by terminal bytes
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (key_timers[i] > 0.f) {
            key_timers[i] -= dt;
            if (key_timers[i] <= 0.f) {
                keys[i] = false;
                key_timers[i] = 0.f;
            }
        }
    }
#endif

    prev_mouse_pos = mouse_pos;
    mouse_delta = {0.f, 0.f};
    mouse_wheel_delta = 0.f;
    text_input_buffer.clear();
    event_queue.clear();

    if (mouse_locked && is_focused_win) {
        POINT center; RECT rect;
        if (GetWindowRect(fg, &rect)) {
            center.x = rect.left + (rect.right  - rect.left) / 2;
            center.y = rect.top  + (rect.bottom - rect.top)  / 2;
            POINT current;
            if (GetCursorPos(&current)) {
                mouse_delta.u = (float)(current.x - center.x) * 0.15f;
                mouse_delta.v = (float)(current.y - center.y) * 0.15f;
                SetCursorPos(center.x, center.y);
            }
        }
        if (!was_locked_and_focused) { while (ShowCursor(FALSE) >= 0); was_locked_and_focused = true; }
    } else {
        if (was_locked_and_focused) { while (ShowCursor(TRUE) < 0); was_locked_and_focused = false; }
    }

    if (is_focused_win) {
        // We still use GetAsyncKeyState for modifiers because it handles
        // global state which might be missed by the console buffer if it's lagging.
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) pushEvent({InputEventType::KeyDown, KEY_CONTROL});
        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) pushEvent({InputEventType::KeyDown, KEY_SHIFT});
        if (GetAsyncKeyState(VK_MENU)    & 0x8000) pushEvent({InputEventType::KeyDown, KEY_ALT});
    }

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hIn, &dwMode);
    dwMode |= ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    dwMode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_ECHO_INPUT |
                ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(hIn, dwMode);

    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(hIn, &numEvents);
    if (numEvents > 0) {
        INPUT_RECORD eventBuffer[512];
        DWORD eventsRead = 0;
        if (ReadConsoleInputW(hIn, eventBuffer,
                              fast_min_t((DWORD)512, numEvents), &eventsRead)) {
            for (DWORD i = 0; i < eventsRead; ++i) {
                switch (eventBuffer[i].EventType) {
                    case FOCUS_EVENT:
                        pushEvent({eventBuffer[i].Event.FocusEvent.bSetFocus ? InputEventType::FocusGain : InputEventType::FocusLost});
                        break;
                    case KEY_EVENT: {
                        const auto& ke = eventBuffer[i].Event.KeyEvent;
                        InputEventType type = ke.bKeyDown ? InputEventType::KeyDown : InputEventType::KeyUp;
                        WORD vk = ke.wVirtualKeyCode;
                        wchar_t wc = ke.uChar.UnicodeChar;

                        int code = -1;
                        if      (vk == VK_BACK)   code = KEY_BACKSPACE;
                        else if (vk == VK_RETURN) code = KEY_ENTER;
                        else if (vk == VK_ESCAPE) code = KEY_ESC;
                        else if (vk == VK_LEFT)   code = KEY_LEFT;
                        else if (vk == VK_RIGHT)  code = KEY_RIGHT;
                        else if (vk == VK_UP)     code = KEY_UP;
                        else if (vk == VK_DOWN)   code = KEY_DOWN;
                        else if (wc >= 32)        code = (unsigned char)toupper(wc);

                        if (code != -1) pushEvent({type, code});
                        
                        if (ke.bKeyDown && wc >= 32) {
                            InputEvent te; te.type = InputEventType::Text;
                            char tmp[8];
                            int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, tmp, sizeof(tmp), nullptr, nullptr);
                            if (len > 0) {
                                memcpy(te.text, tmp, len);
                                pushEvent(te);
                            }
                        }
                        break;
                    }
                    case MOUSE_EVENT: {
                        const auto& me = eventBuffer[i].Event.MouseEvent;
                        pushEvent({InputEventType::MouseMove, 0, (float)me.dwMousePosition.X, (float)me.dwMousePosition.Y});
                        
                        if (me.dwEventFlags == 0 || me.dwEventFlags == DOUBLE_CLICK) {
                            auto pushBtn = [&](int mask, int slot) {
                                bool down = (me.dwButtonState & mask) != 0;
                                bool prev = (prev_mouse_buttons[slot]);
                                if (down != prev) pushEvent({down ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp, slot});
                            };
                            pushBtn(FROM_LEFT_1ST_BUTTON_PRESSED, MOUSE_LEFT);
                            pushBtn(RIGHTMOST_BUTTON_PRESSED,     MOUSE_RIGHT);
                            pushBtn(FROM_LEFT_2ND_BUTTON_PRESSED, MOUSE_MIDDLE);
                        } else if (me.dwEventFlags == MOUSE_WHEELED) {
                            float delta = (short)HIWORD(me.dwButtonState) / (float)WHEEL_DELTA;
                            pushEvent({InputEventType::MouseWheel, 0, delta});
                        }
                        break;
                    }
                }
            }
        }
    }

    processEvents();

    if (!mouse_locked || !is_focused_win) {
        mouse_delta.u = mouse_pos.u - prev_mouse_pos.u;
        mouse_delta.v = mouse_pos.v - prev_mouse_pos.v;
    }
}

#else // ──────────────────────────────────────────────────────────────────────
// Linux / POSIX input
// ─────────────────────────────────────────────────────────────────────────────

// ── Mouse tracking ────────────────────────────────────────────────────────────
// Enable/disable VT200 extended mouse tracking (SGR 1006 mode).
// Gives us button press/release + position in cell coordinates.
// Must be disabled on exit or the terminal stays in mouse mode.
void InputManager::enableMouseTracking(bool enable) {
    if (enable) {
        // SGR extended mouse: supports columns >223 unlike basic X10 mode
        // \x1b[?1000h = button events
        // \x1b[?1002h = button + motion while pressed
        // \x1b[?1006h = SGR encoding (no 223-col limit)
        const char* seq = "\x1b[?1000h\x1b[?1002h\x1b[?1006h";
        if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {}
    } else {
        const char* seq = "\x1b[?1006l\x1b[?1002l\x1b[?1000l";
        if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {}
    }
}

// ── Non-blocking stdin read ────────────────────────────────────────────────────
// Returns bytes read, or 0 if nothing available. Never blocks.
static int read_stdin_nonblock(char* buf, int capacity) {
    // Use select() with zero timeout to check if data is available
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (ready <= 0) return 0;

    int n = (int)read(STDIN_FILENO, buf, (size_t)capacity);
    return (n > 0) ? n : 0;
}

// ── SGR mouse sequence parser ─────────────────────────────────────────────────
// Parses: ESC [ < Pb ; Px ; Py M/m
//   Pb = button code, Px = column (1-based), Py = row (1-based)
//   'M' = press, 'm' = release
// Returns true if consumed, fills out_* fields.
static bool parse_sgr_mouse(const char* seq, int len,
                              int& out_btn, int& out_x, int& out_y, bool& out_release) {
    // seq points just after ESC [ < 
    // format: digits ; digits ; digits M or m
    int btn = 0, x = 0, y = 0;
    int i = 0;
    while (i < len && seq[i] >= '0' && seq[i] <= '9') btn = btn*10 + (seq[i++]-'0');
    if (i >= len || seq[i++] != ';') return false;
    while (i < len && seq[i] >= '0' && seq[i] <= '9') x = x*10 + (seq[i++]-'0');
    if (i >= len || seq[i++] != ';') return false;
    while (i < len && seq[i] >= '0' && seq[i] <= '9') y = y*10 + (seq[i++]-'0');
    if (i >= len || (seq[i] != 'M' && seq[i] != 'm')) return false;

    out_release = (seq[i] == 'm');
    out_btn = btn;
    out_x   = x - 1;  // convert to 0-based
    out_y   = y - 1;
    return true;
}

// ── ANSI escape sequence parser ───────────────────────────────────────────────
// Returns number of bytes consumed from buf (0 if not a recognised sequence).
// ── ANSI escape sequence parser ───────────────────────────────────────────────
// Returns number of bytes consumed from buf (0 if not a recognised sequence).
int InputManager::parseEscapeSequence(const char* buf, int len) {
    if (len < 2) return 0;  // wait for more data

    // ESC [ ...
    if (buf[1] == '[') {
        if (len < 3) return 0;

        // ── SGR mouse: ESC [ < ...
        if (buf[2] == '<') {
            // Find terminator M or m
            for (int i = 3; i < len; ++i) {
                if (buf[i] == 'M' || buf[i] == 'm') {
                    int btn, x, y; bool release;
                    if (parse_sgr_mouse(buf + 3, i - 2, btn, x, y, release)) {
                        pushEvent({InputEventType::MouseMove, 0, (float)x, (float)y});

                        // Decode button:
                        // btn & 64 = wheel event
                        // btn & 32 = motion (no button change)
                        // btn & 3  = 0=left, 1=middle, 2=right, 3=none
                        if (btn & 64) {
                            if (!release) {
                                int dir = (btn & 1) ? -1 : 1;
                                pushEvent({InputEventType::MouseWheel, 0, (float)dir});
                            }
                        } else if (!(btn & 32)) {
                            int b = btn & 3;
                            int slot = -1;
                            if      (b == 0) slot = MOUSE_LEFT;
                            else if (b == 1) slot = MOUSE_MIDDLE;
                            else if (b == 2) slot = MOUSE_RIGHT;
                            if (slot >= 0) pushEvent({release ? InputEventType::MouseButtonUp : InputEventType::MouseButtonDown, slot});
                        }
                    }
                    return i + 1;
                }
            }
            return 0;  // incomplete
        }

        // ── Focus events: ESC [ I (focus in) / ESC [ O (focus out)
        if (len >= 3 && buf[2] == 'I') { pushEvent({InputEventType::FocusGain}); return 3; }
        if (len >= 3 && buf[2] == 'O') { pushEvent({InputEventType::FocusLost}); return 3; }

        // ── Arrow keys + modifiers
        if (len >= 3) {
            for (int i = 2; i < len; ++i) {
                char c = buf[i];
                if (c >= 'A' && c <= 'Z') {
                    int code = -1;
                    if      (c == 'A') code = KEY_UP;
                    else if (c == 'B') code = KEY_DOWN;
                    else if (c == 'C') code = KEY_RIGHT;
                    else if (c == 'D') code = KEY_LEFT;
                    else if (c == 'H') code = KEY_HOME;
                    else if (c == 'F') code = KEY_END;

                    if (code != -1) {
                        pushEvent({InputEventType::KeyDown, code});
                    }

                    for (int j = 2; j < i; ++j) {
                        if (buf[j] == ';' && j+1 < i) {
                            int mod = 0;
                            for (int k = j+1; k < i; ++k)
                                if (buf[k] >= '0' && buf[k] <= '9')
                                    mod = mod*10 + (buf[k]-'0');
                            mod -= 1;
                            if (mod & 1) { pushEvent({InputEventType::KeyDown, KEY_SHIFT});   pushEvent({InputEventType::KeyUp, KEY_SHIFT});   }
                            if (mod & 2) { pushEvent({InputEventType::KeyDown, KEY_ALT});     pushEvent({InputEventType::KeyUp, KEY_ALT});     }
                            if (mod & 4) { pushEvent({InputEventType::KeyDown, KEY_CONTROL}); pushEvent({InputEventType::KeyUp, KEY_CONTROL}); }
                            break;
                        }
                    }
                    return i + 1;
                }
                if (c == '~') {
                    int code = -1;
                    int val = 0;
                    for (int k = 2; k < i; ++k)
                        if (buf[k] >= '0' && buf[k] <= '9')
                            val = val*10 + (buf[k]-'0');
                    switch (val) {
                        case 1:  code = KEY_HOME; break;
                        case 4:  code = KEY_END;  break;
                        case 5:  code = KEY_PGUP; break;
                        case 6:  code = KEY_PGDN; break;
                        case 11: code = KEY_F1;   break;
                        case 12: code = KEY_F2;   break;
                        case 13: code = KEY_F3;   break;
                        case 14: code = KEY_F4;   break;
                        case 15: code = KEY_F5;   break;
                        case 17: code = KEY_F6;   break;
                        case 18: code = KEY_F7;   break;
                        case 19: code = KEY_F8;   break;
                        case 20: code = KEY_F9;   break;
                        case 21: code = KEY_F10;  break;
                        case 23: code = KEY_F11;  break;
                        case 24: code = KEY_F12;  break;
                    }
                    if (code != -1) {
                        pushEvent({InputEventType::KeyDown, code});
                    }
                    return i + 1;
                }
            }
            return 0;
        }
    }

    if (buf[1] == 'O' && len >= 3) {
        int code = -1;
        switch (buf[2]) {
            case 'A': code = KEY_UP;    break;
            case 'B': code = KEY_DOWN;  break;
            case 'C': code = KEY_RIGHT; break;
            case 'D': code = KEY_LEFT;  break;
            case 'P': code = KEY_F1;    break;
            case 'Q': code = KEY_F2;    break;
            case 'R': code = KEY_F3;    break;
            case 'S': code = KEY_F4;    break;
        }
        if (code != -1) {
            pushEvent({InputEventType::KeyDown, code});
        }
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

    for (int i = 0; i < KEY_COUNT; ++i)   prev_keys[i]          = keys[i];
    for (int i = 0; i < MOUSE_COUNT; ++i) prev_mouse_buttons[i] = mouse_buttons[i];

    // Watchdog: release keys that haven't been refreshed by terminal bytes
    for (int i = 0; i < KEY_COUNT; ++i) {
        if (key_timers[i] > 0.f) {
            key_timers[i] -= dt;
            if (key_timers[i] <= 0.f) {
                keys[i] = false;
                key_timers[i] = 0.f;
            }
        }
    }

    prev_mouse_pos    = mouse_pos;
    mouse_delta       = {0.f, 0.f};
    mouse_wheel_delta = 0.f;
    text_input_buffer.clear();
    event_queue.clear();

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
                    pushEvent({InputEventType::KeyDown, KEY_ESC});
                    i += 1;
                } else {
                    i += consumed;
                }
                continue;
            }

            if (c == 0x7f || c == 0x08) {
                pushEvent({InputEventType::KeyDown, KEY_BACKSPACE});
                InputEvent te; te.type = InputEventType::Text; te.text[0] = '\b';
                pushEvent(te);
                i++; continue;
            }
            if (c == '\n' || c == '\r') {
                pushEvent({InputEventType::KeyDown, KEY_ENTER});
                i++; continue;
            }
            if (c == '\t') {
                pushEvent({InputEventType::KeyDown, KEY_TAB});
                i++; continue;
            }

            if (c >= 1 && c <= 26) {
                pushEvent({InputEventType::KeyDown, KEY_CONTROL});
                pushEvent({InputEventType::KeyDown, 'A' + c - 1});
                i++; continue;
            }

            if (c >= 32 && c < 128) {
                pushEvent({InputEventType::KeyDown, (int)toupper((char)c)});
                InputEvent te; te.type = InputEventType::Text; te.text[0] = (char)c;
                pushEvent(te);
                i++; continue;
            }

            if (c >= 0xC0) {
                int char_len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
                char_len = fast_min_t(char_len, bytes - i);
                InputEvent te; te.type = InputEventType::Text;
                memcpy(te.text, buf + i, char_len);
                pushEvent(te);
                i += char_len;
                continue;
            }
            i++;
        }
    }

    processEvents();

    if (!mouse_locked) {
        mouse_delta.u = mouse_pos.u - prev_mouse_pos.u;
        mouse_delta.v = mouse_pos.v - prev_mouse_pos.v;
    }
}

// ── Terminal raw mode ─────────────────────────────────────────────────────────
void InputManager::setTerminalRawMode() {
    tcgetattr(STDIN_FILENO, &original_termios);
    struct termios raw = original_termios;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(tcflag_t)(IXON | IXOFF | ICRNL | INPCK | ISTRIP);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cflag |=  (tcflag_t)(CS8);
    raw.c_cc[VMIN]  = 0;  // non-blocking: return immediately
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // Request focus events (supported by most modern terminals)
    const char* focus_seq = "\x1b[?1004h";
    if (write(STDOUT_FILENO, focus_seq, strlen(focus_seq)) < 0) {}
}

void InputManager::restoreTerminalMode() {
    // Disable focus events
    const char* focus_seq = "\x1b[?1004l";
    if (write(STDOUT_FILENO, focus_seq, strlen(focus_seq)) < 0) {}
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

void InputManager::setMouseLocked(bool locked) {
    mouse_locked = locked;
    // Mouse lock on Linux: hide cursor with ANSI, warp not available in terminal.
    // Relative motion via mouse tracking deltas is used instead.
    if (locked) {
        const char* hide = "\x1b[?25l";
        if (write(STDOUT_FILENO, hide, strlen(hide)) < 0) {}
    } else {
        const char* show = "\x1b[?25h";
        if (write(STDOUT_FILENO, show, strlen(show)) < 0) {}
    }
}

#endif // !_WIN32

// ─────────────────────────────────────────────────────────────────────────────
// Platform-agnostic query methods
// ─────────────────────────────────────────────────────────────────────────────
bool InputManager::isKeyDown(int key_code) const {
    if (key_code >= 0 && key_code < KEY_COUNT) return keys[key_code];
    return false;
}

bool InputManager::isKeyPressed(int key_code) const {
    if (key_code >= 0 && key_code < KEY_COUNT)
        return keys[key_code] && !prev_keys[key_code];
    return false;
}

#ifdef _WIN32
// Windows setMouseLocked is in the Windows block above implicitly via the
// original — provide it here to satisfy the header on Windows too.
void InputManager::setMouseLocked(bool locked) {
    mouse_locked = locked;
    if (!mouse_locked) { while (ShowCursor(TRUE) < 0); }
}
#endif