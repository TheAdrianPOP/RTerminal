#include "gamepad.h"
#include "../math/math_utils.h"
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <Xinput.h>
#elif defined(__linux__)
#  include <linux/joystick.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <cerrno>
#endif

GamepadManager::GamepadManager() {
#if defined(__linux__)
    for (int i = 0; i < kMaxControllers; ++i) js_fds[i] = -1;
#endif
}

GamepadManager::~GamepadManager() {
#if defined(__linux__)
    for (int i = 0; i < kMaxControllers; ++i) {
        if (js_fds[i] >= 0) { ::close(js_fds[i]); js_fds[i] = -1; }
    }
#endif
}

float GamepadManager::applyDeadzone(float v, float dz) {
    if (fast_abs(v) < dz) return 0.f;
    float sign = v > 0.f ? 1.f : -1.f;
    float scaled = sign * (fast_abs(v) - dz) / (1.f - dz);
    return (scaled < -1.f) ? -1.f : ((scaled > 1.f) ? 1.f : scaled);
}

void GamepadManager::update(bool process_input) {
    for (int i = 0; i < kMaxControllers; ++i)
        std::memcpy(states_[i].prevButtons, states_[i].buttons, sizeof(states_[i].buttons));
#ifdef _WIN32
    updateXInput(process_input);
#elif defined(__linux__)
    updateLinuxJoysticks(process_input);
#endif
}

const GamepadState& GamepadManager::state(int player) const {
    static GamepadState empty;
    return (player >= 0 && player < kMaxControllers) ? states_[player] : empty;
}

int GamepadManager::connectedCount() const {
    int n = 0;
    for (const auto& s : states_) if (s.connected) ++n;
    return n;
}

bool GamepadManager::setVibration(int player, uint16_t strong_motor, uint16_t weak_motor) {
    if (player < 0 || player >= kMaxControllers || !states_[player].connected) return false;
#ifdef _WIN32
    if (pfnSetState_) {
        XINPUT_VIBRATION vib{};
        vib.wLeftMotorSpeed  = strong_motor;
        vib.wRightMotorSpeed = weak_motor;
        return reinterpret_cast<DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*)>(pfnSetState_)(player, &vib) == ERROR_SUCCESS;
    }
#endif
    return false;
}

template <typename FuncPtr>
static inline FuncPtr load_proc(HMODULE mod, const char* name) {
    return reinterpret_cast<FuncPtr>(
        reinterpret_cast<void(*)()>(GetProcAddress(mod, name))
    );
}

#ifdef _WIN32
void GamepadManager::updateXInput(bool process_input) {
    using XInputGetState_t = DWORD (WINAPI *)(DWORD, XINPUT_STATE*);
    static XInputGetState_t pfnGetState = nullptr;
    static bool tried_load = false;

    if (!tried_load) {
        tried_load = true;
        HMODULE hMod = LoadLibraryA("xinput1_4.dll");
        if (!hMod) hMod = LoadLibraryA("xinput1_3.dll");
        if (!hMod) hMod = LoadLibraryA("xinput9_1_0.dll");
        if (hMod) {
            pfnGetState  = load_proc<XInputGetState_t>(hMod, "XInputGetState");
            pfnSetState_ = load_proc<XInputSetState_t>(hMod, "XInputSetState");
        }
    }

    if (!pfnGetState) {
        for (int i = 0; i < kMaxControllers; ++i) states_[i].connected = false;
        return;
    }

    for (int i = 0; i < kMaxControllers; ++i) {
        XINPUT_STATE xs{};
        DWORD r = pfnGetState(i, &xs);
        GamepadState& s = states_[i];
        s.connected   = (r == ERROR_SUCCESS);
        s.playerIndex = i;
        if (!s.connected) continue;
        if (!process_input) {
            std::memset(s.buttons, 0, sizeof(s.buttons));
            s.leftTrigger = 0.f; s.rightTrigger = 0.f;
            s.leftX = 0.f; s.leftY = 0.f; s.rightX = 0.f; s.rightY = 0.f;
            continue;
        }
        const auto& gp = xs.Gamepad;
        static constexpr WORD masks[16] = {
            XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
            0, 0,
            XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
            XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
            XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
            XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT
        };
        for (int b = 0; b < 16; ++b) s.buttons[b] = masks[b] ? (gp.wButtons & masks[b]) != 0 : false;
        s.leftTrigger  = gp.bLeftTrigger  / 255.f;
        s.rightTrigger = gp.bRightTrigger / 255.f;
        s.buttons[(int)GamepadButton::LTriggerBtn] = s.leftTrigger  > 0.5f;
        s.buttons[(int)GamepadButton::RTriggerBtn] = s.rightTrigger > 0.5f;
        s.leftX  = applyDeadzone(gp.sThumbLX / 32767.f);
        s.leftY  = applyDeadzone(-gp.sThumbLY / 32767.f);
        s.rightX = applyDeadzone(gp.sThumbRX / 32767.f);
        s.rightY = applyDeadzone(-gp.sThumbRY / 32767.f);
    }
}
#elif defined(__linux__)
void GamepadManager::updateLinuxJoysticks(bool process_input) {
    char dev_path[32];
    for (int i = 0; i < kMaxControllers; ++i) {
        GamepadState& s = states_[i];
        s.playerIndex = i;

        if (js_fds[i] < 0) {
            std::snprintf(dev_path, sizeof(dev_path), "/dev/input/js%d", i);
            js_fds[i] = open(dev_path, O_RDONLY | O_NONBLOCK);
            if (js_fds[i] >= 0) s.connected = true;
        }

        if (js_fds[i] < 0) {
            s.connected = false;
            continue;
        }

        if (!process_input) {
            std::memset(s.buttons, 0, sizeof(s.buttons));
            s.leftTrigger = 0.f; s.rightTrigger = 0.f;
            s.leftX = 0.f; s.leftY = 0.f; s.rightX = 0.f; s.rightY = 0.f;
            continue;
        }

        struct js_event e;
        while (read(js_fds[i], &e, sizeof(e)) > 0) {
            unsigned char type = e.type & ~JS_EVENT_INIT;
            if (type == JS_EVENT_BUTTON) {
                GamepadButton b = GamepadButton::Count;
                switch (e.number) {
                    case 0: b = GamepadButton::A; break;
                    case 1: b = GamepadButton::B; break;
                    case 2: b = GamepadButton::X; break;
                    case 3: b = GamepadButton::Y; break;
                    case 4: b = GamepadButton::LBumper; break;
                    case 5: b = GamepadButton::RBumper; break;
                    case 6: b = GamepadButton::Back; break;
                    case 7: b = GamepadButton::Start; break;
                    case 9: b = GamepadButton::LStick; break;
                    case 10: b = GamepadButton::RStick; break;
                    default: break;
                }
                if (b != GamepadButton::Count) {
                    s.buttons[(int)b] = (e.value != 0);
                }
            } else if (type == JS_EVENT_AXIS) {
                float norm = e.value / 32767.0f;
                switch (e.number) {
                    case 0: s.leftX = applyDeadzone(norm); break;
                    case 1: s.leftY = applyDeadzone(-norm); break;
                    case 2:
                        s.leftTrigger = (norm + 1.0f) * 0.5f;
                        s.buttons[(int)GamepadButton::LTriggerBtn] = s.leftTrigger > 0.5f;
                        break;
                    case 3: s.rightX = applyDeadzone(norm); break;
                    case 4: s.rightY = applyDeadzone(-norm); break;
                    case 5:
                        s.rightTrigger = (norm + 1.0f) * 0.5f;
                        s.buttons[(int)GamepadButton::RTriggerBtn] = s.rightTrigger > 0.5f;
                        break;
                    case 6:
                        s.buttons[(int)GamepadButton::DPadLeft]  = (e.value < -16000);
                        s.buttons[(int)GamepadButton::DPadRight] = (e.value >  16000);
                        break;
                    case 7:
                        s.buttons[(int)GamepadButton::DPadUp]   = (e.value < -16000);
                        s.buttons[(int)GamepadButton::DPadDown] = (e.value >  16000);
                        break;
                    default: break;
                }
            }
        }

        if (errno == ENODEV) {
            ::close(js_fds[i]);
            js_fds[i] = -1;
            s.connected = false;
        }
    }
}
#endif

GamepadManager& globalGamepad() {
    static GamepadManager gm;
    return gm;
}