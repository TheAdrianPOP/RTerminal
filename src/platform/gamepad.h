#pragma once
#ifndef GAMEPAD_H
#define GAMEPAD_H

#include "../math/vector.h"
#include "../math/math_utils.h"
#include <cstring>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#  include <Xinput.h>
#endif

enum class GamepadButton : uint32_t {
    A           = 0,
    B           = 1,
    X           = 2,
    Y           = 3,
    LBumper     = 4,
    RBumper     = 5,
    LTriggerBtn = 6,
    RTriggerBtn = 7,
    Start       = 8,
    Back        = 9,
    LStick      = 10,
    RStick      = 11,
    DPadUp      = 12,
    DPadDown    = 13,
    DPadLeft    = 14,
    DPadRight   = 15,
    Count       = 16
};

struct GamepadState {
    bool  connected      = false;
    bool  buttons[16]    = {};
    bool  prevButtons[16]= {};
    float leftX  = 0.f, leftY  = 0.f;  // [-1, 1]
    float rightX = 0.f, rightY = 0.f;
    float leftTrigger  = 0.f;           // [0, 1]
    float rightTrigger = 0.f;
    int   playerIndex  = 0;

    bool isDown   (GamepadButton b) const { return buttons[(int)b]; }
    bool isPressed(GamepadButton b) const { return buttons[(int)b] && !prevButtons[(int)b]; }
    bool isReleased(GamepadButton b)const { return !buttons[(int)b] && prevButtons[(int)b]; }
    Vec2 leftStick() const  { return {leftX,  leftY};  }
    Vec2 rightStick() const { return {rightX, rightY}; }
};

class GamepadManager {
public:
    static constexpr int kMaxControllers = 4;

    void update(bool process_input = true) {
        for (int i = 0; i < kMaxControllers; ++i)
            memcpy(states_[i].prevButtons, states_[i].buttons, sizeof(states_[i].buttons));
#ifdef _WIN32
        updateXInput(process_input);
#endif
    }

    const GamepadState& state(int player) const {
        static GamepadState empty;
        return (player>=0&&player<kMaxControllers) ? states_[player] : empty;
    }
    bool isConnected(int player) const { return state(player).connected; }
    int  connectedCount() const {
        int n=0; for (auto& s: states_) if (s.connected) ++n; return n;
    }

private:
    GamepadState states_[kMaxControllers];

    static float applyDeadzone(float v, float dz=0.15f) {
        if (fast_abs(v) < dz) return 0.f;
        float sign = v>0?1.f:-1.f;
        return sign * (fast_abs(v)-dz)/(1.f-dz);
    }

#ifdef _WIN32
    typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE*);

    void updateXInput(bool process_input) {
        static XInputGetState_t pfnGetState = nullptr;
        static bool tried_load = false;

        if (!tried_load) {
            tried_load = true;
            HMODULE hMod = LoadLibraryA("xinput1_4.dll");
            if (!hMod) hMod = LoadLibraryA("xinput1_3.dll");
            if (!hMod) hMod = LoadLibraryA("xinput9_1_0.dll");
            if (hMod) pfnGetState = (XInputGetState_t)GetProcAddress(hMod, "XInputGetState");
        }

        if (!pfnGetState) {
            for (int i=0; i<kMaxControllers; ++i) states_[i].connected = false;
            return;
        }

        for (int i=0; i<kMaxControllers; ++i) {
            XINPUT_STATE xs{};
            DWORD r = pfnGetState(i, &xs);
            GamepadState& s = states_[i];
            s.connected   = (r == ERROR_SUCCESS);
            s.playerIndex = i;
            if (!s.connected) continue;
            if (!process_input) {
                memset(s.buttons, 0, sizeof(s.buttons));
                s.leftTrigger = 0.f; s.rightTrigger = 0.f;
                s.leftX = 0.f; s.leftY = 0.f; s.rightX = 0.f; s.rightY = 0.f;
                continue;
            }
            auto& gp = xs.Gamepad;
            constexpr WORD masks[16] = {
                XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
                XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                0, 0,
                XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
                XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
                XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
                XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT
            };
            for (int b=0; b<16; ++b) s.buttons[b] = masks[b] ? (gp.wButtons & masks[b]) != 0 : false;
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
#endif
};

inline GamepadManager& globalGamepad() { static GamepadManager gm; return gm; }

#endif
