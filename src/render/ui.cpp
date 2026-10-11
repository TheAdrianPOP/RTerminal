#include "ui.h"

void UI::Begin(int x, int y, int w, int h) {
    state.clip_x   = x; 
    state.clip_y   = y; 
    state.clip_w   = w; 
    state.clip_h   = h;
    state.window_x = x; 
    state.window_w = w;
    state.cursor_x = x; 
    state.cursor_y = y;
}

void UI::End() {
    state.clip_x = 0; 
    state.clip_y = 0;
    state.clip_w = state.screen_w; 
    state.clip_h = state.screen_h;
}

bool UI::IsClipped(int x, int y) const {
    return ((unsigned)(x - state.clip_x) >= (unsigned)state.clip_w) ||
           ((unsigned)(y - state.clip_y) >= (unsigned)state.clip_h);
}

void UI::SetCell(int x, int y, ConsoleGlyph c, const RGBColor& fg, const RGBColor& bg, bool ui, bool border) {
    if (!state.buffer || state.buffer->empty()) return;
    if (IsClipped(x, y)) return;
    if ((unsigned)y < (unsigned)state.buffer->size()) {
        if ((unsigned)x < (unsigned)(*state.buffer)[y].size()) {
            auto& cell     = (*state.buffer)[y][x];
            cell.character = c;
            cell.color     = fg;
            cell.bg_color  = bg;
            cell.is_ui     = ui;
            cell.is_border = border;
        }
    }
}

void UI::FillRect(int x, int y, int w, int h, ConsoleGlyph c, const RGBColor& fg, const RGBColor& bg) {
    for (int r = y; r < y + h; ++r) {
        for (int col = x; col < x + w; ++col) {
            SetCell(col, r, c, fg, bg);
        }
    }
}

void UI::DrawText(const char* text, const RGBColor& fg, const RGBColor& bg) {
    if (!text) return;
    int cx = state.cursor_x;
    for (int i = 0; text[i]; ++i) {
        if (cx >= state.window_x + state.window_w) break;
        SetCell(cx, state.cursor_y, text[i], fg, bg, true, false);
        cx++;
    }
    state.cursor_x = cx;
}