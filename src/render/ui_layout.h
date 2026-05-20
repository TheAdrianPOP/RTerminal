#pragma once
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

// Immediate-mode UI layout engine — sits on top of DrawCtx.
// Provides automatic layout (vertical/horizontal stacking), anchoring,
// margins, and a small set of higher-level widgets.
//
// Usage:
//   UILayout ui(ctx);               // ctx = DrawCtx
//   ui.begin({5, 3, 40, 20});       // start a panel at x=5,y=3 with w=40,h=20
//   ui.label("Health: 100", {255,80,80});
//   ui.separator();
//   if (ui.button("Start Game"))    startGame();
//   ui.progressBar(hp/maxHp, {0,200,0});
//   ui.end();

#include "draw2d.h"
#include "../platform/input.h"
#include "../math/rect.h"
#include "../math/color.h"

enum class UIDir { Vertical, Horizontal };

struct UIStyle {
    RGBColor fg         = {220,220,220};
    RGBColor bg         = {20, 20, 40};
    RGBColor borderFg   = {100,100,180};
    RGBColor hoverFg    = {255,220,80};
    RGBColor activeFg   = {80, 200,80};
    RGBColor barFill    = {60, 140,60};
    RGBColor barEmpty   = {40, 40, 60};
    int      margin     = 1;          // cells inside the panel border
    bool     drawBorder = true;
};

class UILayout {
public:
    UILayout(DrawCtx& ctx, const InputManager* input = nullptr, UIStyle style = {})
        : ctx_(ctx), input_(input), style_(style) {}

    // ── Panel begin/end ───────────────────────────────────────────────────────

    void begin(Recti panel, UIDir dir = UIDir::Vertical, const char* title = nullptr) {
        panel_  = panel;
        dir_    = dir;
        cursor_ = {panel.x + style_.margin, panel.y + style_.margin};
        if (style_.drawBorder) {
            ctx_.drawRect(panel, 0xDB, style_.borderFg, style_.bg);
        } else {
            ctx_.fillRect(panel, ' ', style_.fg, style_.bg);
        }
        if (title) {
            ctx_.drawText(panel.x + 1, panel.y, title, style_.hoverFg, style_.bg);
        }
        ctx_.setClip(panel.expand(-1));
    }
    void end() { ctx_.resetClip(); }

    // ── Primitive widgets ─────────────────────────────────────────────────────

    void label(const char* text, RGBColor fg = {}) {
        if (fg.isReset()) fg = style_.fg;
        ctx_.drawText(cursor_.x, cursor_.y, text, fg, style_.bg);
        advance(1);
    }

    void separator() {
        int y = cursor_.y;
        for (int x = panel_.x+1; x < panel_.right()-1; ++x)
            ctx_.put(x, y, 0xC4, style_.borderFg, style_.bg); // '─'
        advance(1);
    }

    // Returns true on click/press this frame
    bool button(const char* text, RGBColor fg = {}) {
        if (fg.isReset()) fg = style_.fg;
        bool hover = false, pressed = false;
        if (input_) {
            Vec2 mp = input_->getMousePosition();
            hover = mp.x >= cursor_.x && mp.x < cursor_.x+30 && mp.y == cursor_.y;
            pressed = hover && input_->isMouseButtonPressed(MOUSE_LEFT);
        }
        RGBColor labelFg = hover ? style_.hoverFg : fg;
        ctx_.put(cursor_.x, cursor_.y, '[', style_.borderFg, style_.bg);
        ctx_.drawText(cursor_.x+1, cursor_.y, text, labelFg, style_.bg);
        int len=0; while(text[len]) ++len;
        ctx_.put(cursor_.x+1+len, cursor_.y, ']', style_.borderFg, style_.bg);
        advance(1);
        return pressed;
    }

    // Progress bar — value in [0,1]
    void progressBar(float value, RGBColor fillFg = {}, int width = 20) {
        if (fillFg.isReset()) fillFg = style_.activeFg;
        value = fast_clamp(value, 0.f, 1.f);
        int filled = (int)(value * width);
        for (int i=0; i<width; ++i) {
            RGBColor bg = (i < filled) ? style_.barFill : style_.barEmpty;
            ctx_.put(cursor_.x+i, cursor_.y, ' ', bg, bg);
        }
        advance(1);
    }

    // Checkbox — returns current state
    bool checkbox(const char* label_, bool& value) {
        bool pressed = button(value ? "X" : " ");
        if (pressed) value = !value;
        if (label_) {
            int x = cursor_.x; // cursor already advanced — draw on previous row
            ctx_.drawText(x+3, cursor_.y-1, label_, style_.fg, style_.bg);
        }
        return value;
    }

    // Horizontal spacer (Vertical layout) or vertical (Horizontal layout)
    void space(int cells = 1) { advance(cells); }

    // ── Layout helpers ────────────────────────────────────────────────────────

    void setDir(UIDir d) { dir_ = d; }
    Vec2i cursor() const { return cursor_; }

private:
    void advance(int rows) {
        if (dir_ == UIDir::Vertical)   cursor_.y += rows;
        else                            cursor_.x += rows * 10; // approximate col step
    }

    DrawCtx&          ctx_;
    const InputManager* input_;
    UIStyle           style_;
    Recti             panel_ {};
    UIDir             dir_   = UIDir::Vertical;
    Vec2i             cursor_{};
};

#endif
