#include "ui.h"
#include "math_utils.h"
#include "input.h"
#include "engine_config.h"
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <cstring>
#include "../platform/gamepad.h"

static int GetUtf8CharLength(unsigned char c) {
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}
static bool IsUtf8StartByte(unsigned char c) { return (c & 0xC0) != 0x80; }

void UI::SetCell(ImDrawState& ctx, int x, int y, ConsoleGlyph c, const RGBColor& col, bool ui, bool border)
{
    if (!ctx.buffer || ctx.buffer->empty()) return;
    if ((unsigned)y < (unsigned)ctx.buffer->size()) {
        if ((unsigned)x < (unsigned)(*ctx.buffer)[y].size()) {
            auto& cell    = (*ctx.buffer)[y][x];
            cell.character = c;
            cell.color     = col;
            cell.is_ui     = ui;
            cell.is_border = border;
        }
    }
}

bool UI::IsClipped(int x, int y, const ImDrawState& s) {
    return ((unsigned)(x - s.clip_x) >= (unsigned)s.clip_w) ||
           ((unsigned)(y - s.clip_y) >= (unsigned)s.clip_h);
}

RGBColor UI::FadeColor(const RGBColor& c, float alpha) {
    if (alpha >= 1.0f) return c;
    if (alpha <= 0.0f) return {0, 0, 0};
    return { (int)(c.r * alpha), (int)(c.g * alpha), (int)(c.b * alpha) };
}

UI::UI() {
    fps_timer.start();
    memset(frame_time_history, 0, sizeof(frame_time_history));
}
void UI::setMemoryStats(size_t cpu_bytes)     { cpu_memory_usage = cpu_bytes; }
void UI::setCameraStats(float yaw, float pitch){ cam_yaw = yaw; cam_pitch = pitch; }
void UI::setUpdateTime(double ms)             { update_ms = ms; }
void UI::setRenderTime(double ms)             { render_ms = ms; }
void UI::setTriangleRenderTime(double ms)     { triangle_render_ms = ms; }
void UI::setUiRenderTime(double ms)           { ui_render_ms = ms; }
void UI::setBvhIntersectionTime(double ms)    { bvh_intersection_ms = ms; }
void UI::setBVHStats(int n, int p)            { bvh_nodes = n; bvh_packed = p; }

void UI::addMessage(const char* message, const RGBColor& color) {
    if (!message_log.empty() && message_log.back().text == message) {
        message_log.back().life      = 10.0f;
        message_log.back().timestamp = global_time;
        return;
    }
    if (message_log.size() >= MAX_MESSAGES) {
        message_log.erase(message_log.begin());
    }
    LogMessage msg;
    msg.text      = message;
    msg.color     = color;
    msg.life      = 10.0f;
    msg.max_life  = 10.0f;
    msg.timestamp = global_time;
    message_log.push_back(msg);
}

void UI::setMenuOpen(bool open) {
    menu.is_active = open;
    if (open) {
        menu.selected_index      = 0;
        menu.scroll_offset       = 0;
        menu.nav_focus_tab_header = true;
    }
}

void UI::update(float deltaTime, const Vec3& player_pos, const Vec3& camera_dir,
                float fps, const Vec3& vel, bool grounded, bool show_ui)
{
    current_player_pos = player_pos;
    current_camera_dir = camera_dir;
    current_velocity   = vel;
    is_grounded        = grounded;
    global_time       += deltaTime;

    for (size_t i = 0; i < message_log.size(); ) {
        message_log[i].life -= deltaTime;
        if (message_log[i].life <= 0.0f) message_log.erase(message_log.begin() + i);
        else ++i;
    }

    frame_time_history[history_offset] = deltaTime * 1000.0f;
    history_offset = (history_offset + 1) % HISTORY_SIZE;

    float menu_target = menu.is_active ? 1.0f : 0.0f;
    float menu_diff   = menu_target - menu.open_anim_t;
    menu.open_anim_t += menu_diff * 8.0f * deltaTime;
    if (fast_abs(menu_diff) < 0.005f) menu.open_anim_t = menu_target;
    menu.open_anim_t = fast_clamp(menu.open_anim_t, 0.0f, 1.0f);

    bool  show_hud  = (!menu.is_active && show_ui);
    float hud_target = show_hud ? 1.0f : 0.0f;
    float hud_speed  = (hud_target > 0.5f) ? 5.0f : 12.0f;
    float hud_diff   = hud_target - menu.hud_anim_t;
    menu.hud_anim_t += hud_diff * hud_speed * deltaTime;
    if (fast_abs(hud_diff) < 0.005f) menu.hud_anim_t = hud_target;
    menu.hud_anim_t = fast_clamp(menu.hud_anim_t, 0.0f, 1.0f);

    frame_counter++;
    double elapsed = fps_timer.getElapsedMs();
    if (elapsed >= 250.0) {
        fps_display_value = fps;
        frame_time_ms     = (float)(elapsed / frame_counter);
        frame_counter     = 0;
        fps_timer.start();
    }
}

void UI::Begin(const char* title, int x, int y, int w, int h, const RGBColor& border_col) {
    ctx.clip_x = x; ctx.clip_y = y; ctx.clip_w = w; ctx.clip_h = h;
    ctx.window_start_x = x; ctx.window_width = w;
    ctx.cursor_x = x + 2; ctx.cursor_y = y + 1;

    int end_y = y + h, end_x = x + w;

    for (int sy = y + 1; sy < end_y + 1; ++sy) {
        if (sy >= end_y) {
            for (int sx = x + 1; sx < end_x + 1; ++sx)
                SetCell(ctx, sx, sy, ' ', Colors::SHADOW, true, false);
        } else {
            SetCell(ctx, end_x, sy, ' ', Colors::SHADOW, true, false);
        }
    }

    for (int r = y; r < end_y; ++r) {
        bool is_edge_y = (r == y || r == end_y - 1);
        for (int c = x; c < end_x; ++c) {
            bool is_edge_x = (c == x || c == end_x - 1);
            if (is_edge_y || is_edge_x) {
                char ch = (is_edge_y && is_edge_x) ? '+' : (is_edge_y ? '-' : '|');
                SetCell(ctx, c, r, ch, border_col, true, true);
            } else {
                SetCell(ctx, c, r, ' ', Colors::MENU_BG, true, false);
            }
        }
    }

    if (title) {
        int len = 0; while (title[len]) len++;
        int tx = x + (w - len) / 2;
        for (int i = 0; i < len; ++i)
            SetCell(ctx, tx + i, y, title[i], Colors::TITLE, true, false);
    }
}

void UI::End() {
    ctx.clip_x = 0; ctx.clip_y = 0;
    ctx.clip_w = ctx.screen_w; ctx.clip_h = ctx.screen_h;
}

void UI::TextColored(const RGBColor& col, const char* fmt, ...) {
    char buf[256]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    int cx = ctx.cursor_x;
    for (int i = 0; buf[i]; ++i) {
        if (cx >= ctx.window_start_x + ctx.window_width - 1) break;
        if (!IsClipped(cx, ctx.cursor_y, ctx)) SetCell(ctx, cx, ctx.cursor_y, buf[i], col, true, false);
        cx++;
    }
    ctx.cursor_y++;
}

void UI::Text(const char* fmt, ...) {
    char buf[256]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    TextColored(Colors::TEXT_DEF, "%s", buf);
}

void UI::DrawKeyValue(const char* label, const char* valStr, const RGBColor& valCol) {
    if (label) TextColored(Colors::LABEL, "%-10s", label);
    if (!valStr) return;
    ctx.cursor_y--;
    int cx = ctx.cursor_x + 12;
    for (int i = 0; valStr[i]; ++i) {
        if (!IsClipped(cx, ctx.cursor_y, ctx))
            SetCell(ctx, cx, ctx.cursor_y, valStr[i], valCol, true, false);
        cx++;
    }
    ctx.cursor_y++;
}

void UI::Value(const char* l, float v)              { char b[64]; snprintf(b,64,"%.3f",v);             DrawKeyValue(l,b,Colors::VALUE); }
void UI::Value(const char* l, const Vec3& v)        { char b[64]; snprintf(b,64,"%.1f, %.1f, %.1f",v.x,v.y,v.z); DrawKeyValue(l,b,Colors::VALUE); }
void UI::Value(const char* l, int v)                { char b[64]; snprintf(b,64,"%d",v);               DrawKeyValue(l,b,Colors::VALUE); }
void UI::Value(const char* l, const char* v)        { DrawKeyValue(l,v,Colors::VALUE); }
void UI::Value(const char* l, const StrBuf<256>& v) { DrawKeyValue(l,v.c_str(),Colors::VALUE); }
void UI::ValueBool(const char* l, bool v)           { DrawKeyValue(l, v?"YES":"NO", v?Colors::OK:Colors::WARN); }

void UI::ValueBytes(const char* l, size_t bytes) {
    char buf[64];
    if      (bytes < 1024)          snprintf(buf, 64, "%zu B",    bytes);
    else if (bytes < 1024*1024)     snprintf(buf, 64, "%.2f KB",  bytes/1024.0);
    else                            snprintf(buf, 64, "%.2f MB",  bytes/(1024.0*1024.0));
    DrawKeyValue(l, buf, Colors::VALUE);
}

void UI::Separator() {
    int end_x = ctx.window_start_x + ctx.window_width - 1;
    for (int x = ctx.window_start_x + 1; x < end_x; ++x)
        if (!IsClipped(x, ctx.cursor_y, ctx))
            SetCell(ctx, x, ctx.cursor_y, '-', Colors::BORDER, true, false);
    ctx.cursor_y++;
}

void UI::ProgressBar(float fraction, int width, const RGBColor& fill_col) {
    int cx = ctx.cursor_x;
    fraction = fast_clamp(fraction, 0.0f, 1.0f);
    int fill_w = (int)(fraction * width);
    for (int i = 0; i < width; ++i) {
        if (!IsClipped(cx + i, ctx.cursor_y, ctx)) {
            char c = (i < fill_w ? '#' : '.');
            RGBColor col = (i < fill_w ? fill_col : RGBColor{50, 50, 50});
            SetCell(ctx, cx + i, ctx.cursor_y, c, col, true, false);
        }
    }
    ctx.cursor_y++;
}

void UI::PlotLines(const char* label, const float* values, int count,
                   int offset, float min_scale, float max_scale, int height)
{
    TextColored(Colors::LABEL, "%s", label);
    int start_x = ctx.cursor_x;
    int width   = fast_min_t(count, ctx.window_width - 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (!IsClipped(start_x+x, ctx.cursor_y+y, ctx))
                SetCell(ctx, start_x+x, ctx.cursor_y+y, ' ', Colors::PLOT_BG, true, false);

    const char* ramp  = "_.-=^";
    float       scale = 1.0f / (max_scale - min_scale);
    for (int i = 0; i < width; ++i) {
        int   idx      = (offset + i) % count;
        float v        = values[idx];
        float t        = fast_clamp((v - min_scale) * scale, 0.0f, 1.0f);
        int   row      = fast_clamp((int)((1.0f - t) * (height - 1)), 0, height - 1);
        int   char_idx = (int)(t * 4.0f) % 5;
        if (!IsClipped(start_x + i, ctx.cursor_y + row, ctx))
            SetCell(ctx, start_x + i, ctx.cursor_y + row, ramp[char_idx], Colors::PLOT_LINE, true, false);
    }
    ctx.cursor_y += height;
    TextColored({160,160,160}, "Min:%.1f Max:%.1f", min_scale, max_scale);
}

void UI::Dummy(int h) { ctx.cursor_y += h; }

bool UI::isMouseOver(int x, int y, int w, int h) const {
    if (!input_ptr) return false;
    Vec2 mp = input_ptr->getMousePosition();
    return (mp.u >= x && mp.u < x + w && mp.v >= y && mp.v < y + h);
}

bool UI::MenuItem(const char* label) {
    if (menu.nav_focus_tab_header) { TextColored(Colors::INACTIVE, "  %s", label); return false; }
    int idx = menu.item_count++; bool selected = (idx == menu.selected_index);
    if (!menu.nav_focus_tab_header && isMouseOver(ctx.window_start_x + 1, ctx.cursor_y, ctx.window_width - 2, 1)) {
        menu.selected_index = idx; selected = true;
    }
    RGBColor fg = selected ? Colors::SELECTED : Colors::TEXT_DEF;
    if (selected) {
        float pulse = (float)(fast_sin((float)(global_time * 5.0)) * 0.5f + 0.5f);
        RGBColor bg = { (int)(50 + 40*pulse), (int)(50 + 40*pulse), (int)(65 + 40*pulse) };
        fg = { 255, 255, (int)(150 * pulse) };
        int end_x = ctx.window_start_x + ctx.window_width - 1;
        for (int x = ctx.window_start_x + 1; x < end_x; ++x)
            if (!IsClipped(x, ctx.cursor_y, ctx)) SetCell(ctx, x, ctx.cursor_y, ' ', bg, true, false);
        TextColored(fg, "> %s", label);
    } else { TextColored(fg, "  %s", label); }
    return selected;
}

void UI::MenuToggle(const char* label, bool* value) {
    if (!input_ptr || !value) return;
    if (MenuItem(label)) {
        bool mouse_clicked = input_ptr->isMouseButtonPressed(MOUSE_LEFT) &&
                             isMouseOver(ctx.window_start_x + 1, ctx.cursor_y - 1, ctx.window_width - 2, 1);
        const auto& gp = globalGamepad().state(0);
        if (input_ptr->isKeyPressed(KEY_LEFT) || gp.isPressed(GamepadButton::DPadLeft) || 
            input_ptr->isKeyPressed(KEY_RIGHT) || gp.isPressed(GamepadButton::DPadRight) ||
            input_ptr->isKeyPressed(KEY_SPACE) || gp.isPressed(GamepadButton::A) || mouse_clicked)
            *value = !(*value);
    }
    ctx.cursor_y--;
    int cx = ctx.window_start_x + ctx.window_width - 8; ctx.cursor_x = cx;
    TextColored(*value ? Colors::OK : Colors::WARN, "%s", *value ? "[ON ]" : "[OFF]");
    ctx.cursor_x = ctx.window_start_x + 2;
}

void UI::MenuSlider(const char* label, float* value, float mn, float mx, float step) {
    if (!input_ptr || !value) return;
    if (MenuItem(label)) {
        const auto& gp = globalGamepad().state(0);
        if (menu.slider_edit_mode) {
            if (input_ptr->isKeyDown(KEY_LEFT) || gp.isDown(GamepadButton::DPadLeft))  *value -= step;
            if (input_ptr->isKeyDown(KEY_RIGHT) || gp.isDown(GamepadButton::DPadRight)) *value += step;
            *value = fast_clamp(*value, mn, mx);
        }
        if (input_ptr->isMouseButtonDown(MOUSE_LEFT) &&
            isMouseOver(ctx.window_start_x + 1, ctx.cursor_y - 1, ctx.window_width - 2, 1)) {
            int bar_width = 10;
            int cx2 = ctx.window_start_x + ctx.window_width - bar_width - 8;
            Vec2 mp = input_ptr->getMousePosition();
            float t = (mp.u - (cx2 + 1)) / (float)(bar_width - 1);
            if (t >= 0.0f && t <= 1.0f) *value = fast_clamp(mn + t * (mx - mn), mn, mx);
        }
    }
    ctx.cursor_y--;
    int bar_width = 10; int cx = ctx.window_start_x + ctx.window_width - bar_width - 8;
    float normalized = fast_clamp((*value - mn) / (mx - mn), 0.0f, 1.0f);
    int fill = (int)(normalized * bar_width);
    ctx.cursor_x = cx; TextColored({100,100,100}, "["); ctx.cursor_y--; ctx.cursor_x = cx + 1;
    for (int i = 0; i < bar_width; ++i) {
        char c = (i < fill) ? '|' : '.'; RGBColor col = (i < fill) ? Colors::BAR_FILL : Colors::BAR_EMPTY;
        if (!IsClipped(ctx.cursor_x, ctx.cursor_y, ctx)) SetCell(ctx, ctx.cursor_x, ctx.cursor_y, c, col, true, false);
        ctx.cursor_x++;
    }
    TextColored({100,100,100}, "]"); ctx.cursor_y--;
    ctx.cursor_x = cx + bar_width + 2; TextColored(Colors::VALUE, "%5.2f", *value);
    ctx.cursor_x = ctx.window_start_x + 2;
}

void UI::MenuSlider(const char* label, int* value, int mn, int mx, int step) {
    if (!input_ptr || !value) return;
    float v = (float)*value;
    if (MenuItem(label)) {
        const auto& gp = globalGamepad().state(0);
        if (menu.slider_edit_mode) {
            if (input_ptr->isKeyDown(KEY_LEFT) || gp.isDown(GamepadButton::DPadLeft))  v -= (float)step;
            if (input_ptr->isKeyDown(KEY_RIGHT) || gp.isDown(GamepadButton::DPadRight)) v += (float)step;
            v = fast_clamp(v, (float)mn, (float)mx);
        }
        if (input_ptr->isMouseButtonDown(MOUSE_LEFT) &&
            isMouseOver(ctx.window_start_x + 1, ctx.cursor_y - 1, ctx.window_width - 2, 1)) {
            int bar_width = 10;
            int cx2 = ctx.window_start_x + ctx.window_width - bar_width - 8;
            Vec2 mp = input_ptr->getMousePosition();
            float t = (mp.u - (float)(cx2 + 1)) / (float)(bar_width - 1);
            if (t >= 0.0f && t <= 1.0f) v = fast_clamp((float)mn + t * (float)(mx - mn), (float)mn, (float)mx);
        }
    }
    *value = (int)v;
    ctx.cursor_y--;
    int bar_width = 10; int cx = ctx.window_start_x + ctx.window_width - bar_width - 8;
    float normalized = fast_clamp((v - mn) / (float)(mx - mn), 0.0f, 1.0f);
    int fill = (int)(normalized * bar_width);
    ctx.cursor_x = cx; TextColored({100,100,100}, "["); ctx.cursor_y--; ctx.cursor_x = cx + 1;
    for (int i = 0; i < bar_width; ++i) {
        char c = (i < fill) ? '|' : '.'; RGBColor col = (i < fill) ? Colors::BAR_FILL : Colors::BAR_EMPTY;
        if (!IsClipped(ctx.cursor_x, ctx.cursor_y, ctx)) SetCell(ctx, ctx.cursor_x, ctx.cursor_y, c, col, true, false);
        ctx.cursor_x++;
    }
    TextColored({100,100,100}, "]"); ctx.cursor_y--;
    ctx.cursor_x = cx + bar_width + 2; TextColored(Colors::VALUE, "%d", *value);
    ctx.cursor_x = ctx.window_start_x + 2;
}

void UI::MenuTextInput(const char* label, StrBuf<256>* value) {
    if (!input_ptr || !value) return;
    const auto& gp = globalGamepad().state(0);
    if (MenuItem(label)) {
        if (!menu.edit_ramp_mode && (input_ptr->isKeyPressed(KEY_SPACE) || input_ptr->isKeyPressed(KEY_ENTER) || gp.isPressed(GamepadButton::A))) {
            menu.edit_ramp_mode   = true;
            menu.slider_edit_mode = false;
            menu.ramp_cursor_pos  = value->length();
        }
    }
    bool selected = (menu.item_count - 1 == menu.selected_index);
    if (selected && menu.edit_ramp_mode) {
        if (input_ptr->isKeyPressed(KEY_LEFT) || gp.isPressed(GamepadButton::DPadLeft)) {
            int prev_pos = menu.ramp_cursor_pos - 1;
            while (prev_pos > 0 && !IsUtf8StartByte((unsigned char)(*value)[prev_pos])) prev_pos--;
            if (prev_pos >= 0) menu.ramp_cursor_pos = prev_pos;
        }
        if (input_ptr->isKeyPressed(KEY_RIGHT) || gp.isPressed(GamepadButton::DPadRight)) {
            if (menu.ramp_cursor_pos < value->length()) {
                unsigned char c = (unsigned char)(*value)[menu.ramp_cursor_pos];
                menu.ramp_cursor_pos += GetUtf8CharLength(c);
            }
        }
        const char* typed = input_ptr->getTextInput();
        for (int ti = 0; typed[ti]; ++ti) {
            char c = typed[ti];
            if (c == '\b') {
                if (menu.ramp_cursor_pos > 0 && !value->empty()) {
                    int prev_pos = menu.ramp_cursor_pos - 1;
                    while (prev_pos > 0 && !IsUtf8StartByte((unsigned char)(*value)[prev_pos])) prev_pos--;
                    int count = menu.ramp_cursor_pos - prev_pos;
                    value->erase(prev_pos, count);
                    menu.ramp_cursor_pos = prev_pos;
                }
            } else if ((unsigned char)c >= 32) {
                value->insert(menu.ramp_cursor_pos, 1, c);
                menu.ramp_cursor_pos++;
            }
        }
        if (input_ptr->isKeyPressed(KEY_ENTER) || input_ptr->isKeyPressed(KEY_ESC) || gp.isPressed(GamepadButton::A) || gp.isPressed(GamepadButton::B))
            menu.edit_ramp_mode = false;

        ctx.cursor_x = ctx.window_start_x + 2;
        int box_w = ctx.window_width - 4;
        int visible_cells = 0, byte_idx = 0;
        const StrBuf<256>& str = *value;
        while (visible_cells < box_w && byte_idx < str.length()) {
            unsigned char c = (unsigned char)str[byte_idx];
            int char_len = GetUtf8CharLength(c);
            if (byte_idx + char_len > str.length()) break;
            char glyphBuf[5] = {0};
            for (int k = 0; k < char_len; ++k) glyphBuf[k] = str[byte_idx + k];
            bool is_cursor = (byte_idx == menu.ramp_cursor_pos);
            if (is_cursor && (int)(global_time * 3.0) % 2 == 0) is_cursor = false;
            RGBColor col = is_cursor ? Colors::CURSOR : Colors::VALUE;
            if (!IsClipped(ctx.cursor_x + visible_cells, ctx.cursor_y, ctx))
                SetCell(ctx, ctx.cursor_x + visible_cells, ctx.cursor_y,
                        ConsoleGlyph(glyphBuf), is_cursor ? RGBColor{0,0,0} : col, true, false);
            byte_idx += char_len; visible_cells++;
        }
        if (menu.ramp_cursor_pos >= str.length() && visible_cells < box_w)
            if ((int)(global_time * 3.0) % 2 == 0)
                SetCell(ctx, ctx.cursor_x + visible_cells, ctx.cursor_y, '_', Colors::CURSOR, true, false);
        ctx.cursor_y++;
        TextColored(Colors::INACTIVE, "  [EDITING] Type chars...");
    } else {
        ctx.cursor_x = ctx.window_start_x + 2;
        int visible_cells = 0, byte_idx = 0, max_w = ctx.window_width - 4;
        const StrBuf<256>& str = *value;
        while (visible_cells < max_w && byte_idx < str.length()) {
            unsigned char c = (unsigned char)str[byte_idx];
            int char_len = GetUtf8CharLength(c);
            if (byte_idx + char_len > str.length()) break;
            char glyphBuf[5] = {0};
            for (int k = 0; k < char_len; ++k) glyphBuf[k] = str[byte_idx + k];
            if (!IsClipped(ctx.cursor_x + visible_cells, ctx.cursor_y, ctx))
                SetCell(ctx, ctx.cursor_x + visible_cells, ctx.cursor_y,
                        ConsoleGlyph(glyphBuf), Colors::VALUE, true, false);
            byte_idx += char_len; visible_cells++;
        }
        ctx.cursor_y++;
    }
}

void UI::MenuAction(const char* label, bool* triggered) {
    if (!input_ptr) return;
    if (MenuItem(label)) {
        bool mouse_clicked = input_ptr->isMouseButtonPressed(MOUSE_LEFT) &&
                             isMouseOver(ctx.window_start_x + 1, ctx.cursor_y - 1, ctx.window_width - 2, 1);
        const auto& gp = globalGamepad().state(0);
        if (input_ptr->isKeyPressed(KEY_SPACE) || gp.isPressed(GamepadButton::A) || mouse_clicked) *triggered = true;
    }
}

void UI::DrawTabs(int width) {
    const char* tabs[] = {"GAME","PHYSICS","WORLD","CLOUDS","SKY COLOR","GRAPHICS","SYSTEM","INPUT"};
    int tab_count    = 8;
    int start_y      = ctx.cursor_y;
    int sidebar_width = width / 4;
    int sep_x        = ctx.window_start_x + sidebar_width;

    for (int y = start_y; y < start_y + 20; ++y)
        if (!IsClipped(sep_x, y, ctx))
            SetCell(ctx, sep_x, y, '|', Colors::BORDER, true, false);

    for (int i = 0; i < tab_count; ++i) {
        bool is_current = (i == (int)menu.current_tab);
        int  item_y     = start_y + i * 2;
        ctx.cursor_x = ctx.window_start_x + 2;
        ctx.cursor_y = item_y;
        RGBColor tcol = is_current ? Colors::TAB_ACTIVE : Colors::TAB_IDLE;
        if (menu.nav_focus_tab_header && is_current) tcol = Colors::SELECTED;
        TextColored(tcol, is_current ? "> %s" : "  %s", tabs[i]);

        if (input_ptr && isMouseOver(ctx.window_start_x + 1, item_y, sidebar_width - 1, 1)) {
            if (input_ptr->isMouseButtonPressed(MOUSE_LEFT)) {
                menu.current_tab       = (MenuTab)i;
                menu.content_anim_t    = 0.0f;
                menu.selected_index    = 0;
                menu.scroll_offset     = 0;
                menu.nav_focus_tab_header = true;
            }
        }
    }
    ctx.window_start_x += sidebar_width + 1;
    ctx.window_width   -= (sidebar_width + 1);
    ctx.cursor_y = start_y;
    ctx.cursor_x = ctx.window_start_x + 2;
}