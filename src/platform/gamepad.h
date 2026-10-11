#pragma once
#ifndef GAMEPAD_H
#define GAMEPAD_H

#include "../math/vector.h"
#include <cstdint>

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
    bool  connected       = false;
    bool  buttons[16]     = {};
    bool  prevButtons[16] = {};
    float leftX  = 0.f, leftY  = 0.f;  // [-1, 1]
    float rightX = 0.f, rightY = 0.f;
    float leftTrigger  = 0.f;          // [0, 1]
    float rightTrigger = 0.f;
    int   playerIndex  = 0;

    bool isDown   (GamepadButton b) const { return (int)b < 16 ? buttons[(int)b] : false; }
    bool isPressed(GamepadButton b) const { return (int)b < 16 ? (buttons[(int)b] && !prevButtons[(int)b]) : false; }
    bool isReleased(GamepadButton b)const { return (int)b < 16 ? (!buttons[(int)b] && prevButtons[(int)b]) : false; }
    Vec2 leftStick() const  { return {leftX,  leftY};  }
    Vec2 rightStick() const { return {rightX, rightY}; }
};

class GamepadManager {
public:
    static constexpr int kMaxControllers = 4;

    GamepadManager();
    ~GamepadManager();

    void update(bool process_input = true);
    const GamepadState& state(int player) const;
    bool isConnected(int player) const { return state(player).connected; }
    int  connectedCount() const;
    bool setVibration(int player, uint16_t strong_motor, uint16_t weak_motor);

private:
    GamepadState states_[kMaxControllers];

    static float applyDeadzone(float v, float dz = 0.15f);

#ifdef _WIN32
    using XInputGetState_t = unsigned long (__stdcall*)(unsigned long, void*);
    using XInputSetState_t = unsigned long (__stdcall*)(unsigned long, void*);
    XInputSetState_t pfnSetState_ = nullptr;
    void updateXInput(bool process_input);
#elif defined(__linux__)
    int js_fds[kMaxControllers] = {-1, -1, -1, -1};
    void updateLinuxJoysticks(bool process_input);
#endif
};

// Declared only (defined once in engine/platform/input.cpp)
GamepadManager& globalGamepad();

#endif // GAMEPAD_H