#pragma once
#ifndef UI_H
#define UI_H

#include "console.h"
#include "vector.h"

struct ImDrawState {
    int cursor_x = 0;
    int cursor_y = 0;
    int window_x = 0;
    int window_w = 0;
    int screen_w = 0;
    int screen_h = 0;
    int clip_x   = 0;
    int clip_y   = 0;
    int clip_w   = 0;
    int clip_h   = 0;
    PodVector<PodVector<ConsoleCell>>* buffer = nullptr;
};

class UI {
public:
    UI() = default;
    ~UI() = default;

    // Direct buffer setup
    void SetBuffer(PodVector<PodVector<ConsoleCell>>* buf, int w, int h) {
        state.buffer   = buf;
        state.screen_w = w;
        state.screen_h = h;
        state.clip_w   = w;
        state.clip_h   = h;
    }

    // Clipping region configuration
    void Begin(int x, int y, int w, int h);
    void End();

    // Raw draw commands
    void SetCell(int x, int y, ConsoleGlyph c, const RGBColor& fg, const RGBColor& bg, bool ui = true, bool border = false);
    void FillRect(int x, int y, int w, int h, ConsoleGlyph c, const RGBColor& fg, const RGBColor& bg);
    void DrawText(const char* text, const RGBColor& fg, const RGBColor& bg);

    // Coordinate status & tracking
    bool IsClipped(int x, int y) const;
    void SetCursor(int x, int y) { state.cursor_x = x; state.cursor_y = y; }
    void GetCursor(int& x, int& y) const { x = state.cursor_x; y = state.cursor_y; }
    void NewLine() { state.cursor_y++; state.cursor_x = state.window_x; }

protected:
    ImDrawState state;
};

#endif // UI_H