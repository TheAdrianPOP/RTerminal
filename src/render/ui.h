#pragma once

#include <cstdarg>
#include "perf_timer.h"
#include "console.h"
#include "vector.h"
#include "str.h"

class InputManager;

struct ImDrawState {
    int cursor_x = 0; int cursor_y = 0;
    int window_start_x = 0; int window_width = 0;
    int screen_w = 0; int screen_h = 0;
    int clip_x = 0; int clip_y = 0; int clip_w = 0; int clip_h = 0;
    PodVector<PodVector<ConsoleCell>>* buffer = nullptr;
};

namespace Colors {
    inline const RGBColor TEXT_DEF   = {230, 230, 240};
    inline const RGBColor LABEL      = {170, 190, 210};
    inline const RGBColor VALUE      = {255, 255, 220};
    inline const RGBColor TITLE      = {230, 230, 255};
    inline const RGBColor BG         = {30,  35,  45 };
    inline const RGBColor BORDER     = {100, 120, 140};
    inline const RGBColor WARN       = {255, 170, 80 };
    inline const RGBColor OK         = {120, 255, 120};
    inline const RGBColor ERR        = {255, 100, 100};
    inline const RGBColor INACTIVE   = {140, 140, 150};
    inline const RGBColor SELECTED   = {255, 255, 50 };
    inline const RGBColor MENU_BG    = {20,  20,  30 };
    inline const RGBColor TAB_ACTIVE = {255, 255, 255};
    inline const RGBColor TAB_IDLE   = {140, 140, 160};
    inline const RGBColor TAB_BG     = {35,  35,  50 };
    inline const RGBColor SHADOW     = {10,  10,  15 };
    inline const RGBColor BAR_FILL   = {100, 200, 255};
    inline const RGBColor BAR_EMPTY  = {60,  60,  80 };
    inline const RGBColor PLOT_LINE  = {50,  255, 255};
    inline const RGBColor PLOT_BG    = {15,  25,  35 };
    inline const RGBColor STATUS_BG  = {15,  15,  25 };
    inline const RGBColor STATUS_KEY = {100, 100, 110};
    inline const RGBColor STATUS_ACT = {255, 220, 50 };
    inline const RGBColor SCROLL_BG  = {40,  45,  60 };
    inline const RGBColor SCROLL_BAR = {120, 140, 160};
    inline const RGBColor CURSOR     = {255, 215, 0  };
}

class UI {
public:
    UI();
    virtual ~UI() = default;
    void bindInput(const InputManager* inp) { input_ptr = inp; }

    void update(float dt, const Vec3& pos, const Vec3& dir,
                float fps, const Vec3& vel, bool grounded, bool show_ui);

    virtual void render(PodVector<PodVector<ConsoleCell>>& buffer, int w, int h) {}

    void toggleVisibility()        { global_ui_visible = !global_ui_visible; }
    bool isVisible()         const { return global_ui_visible; }
    void setMenuOpen(bool open);
    bool isMenuOpen()        const { return menu.is_active; }

    void addMessage(const char* m, const RGBColor& c = {200, 200, 200});
    void setTriangleCount(int c)        { triangle_count = c; }
    void setBackgroundColorMode(bool)   {}
    void setMemoryStats(size_t cpu_bytes);
    void setCameraStats(float yaw, float pitch);
    void setUpdateTime(double ms);
    void setRenderTime(double ms);
    void setPhysicsTime(double ms)      { physics_ms = ms; }
    void setAnimationTime(double ms)    { animation_ms = ms; }
    void setSceneUpdateTime(double ms)  { scene_update_ms = ms; }
    void setBvhBuildTime(double ms)     { bvh_build_ms = ms; }
    void setShadingTime(double ms)      { shading_ms = ms; }
    void setSkyboxTime(double ms)       { skybox_ms = ms; }
    void setNoiseTime(double ms)        { noise_ms = ms; }
    void setDisplayFlushTime(double ms) { display_flush_ms = ms; }
    void setBVHStats(int n, int p);
    void setTriangleRenderTime(double ms);
    void setUiRenderTime(double ms);
    void setBvhIntersectionTime(double ms);

protected:
    enum class MenuTab { GAME, PHYSICS, WORLD, CLOUDS, SKY_COLOR, GRAPHICS, SYSTEM, INPUT, COUNT };

    struct LogMessage {
        StrBuf<128> text; RGBColor color; float life; float max_life; double timestamp;
    };

    struct MenuState {
        int  selected_index = 0; int item_count = 0; int scroll_offset = 0;
        bool is_active = false;  MenuTab current_tab = MenuTab::GAME;
        bool nav_focus_tab_header = false; bool slider_edit_mode = false;
        bool edit_ramp_mode = false; int ramp_cursor_pos = 0;
        float open_anim_t = 0.0f; float hud_anim_t = 0.0f; float content_anim_t = 0.0f;
    };

    static constexpr size_t MAX_MESSAGES = 8;
    static constexpr size_t HISTORY_SIZE = 60;

    ImDrawState ctx;
    MenuState   menu;

    PodVector<LogMessage> message_log;
    float frame_time_history[HISTORY_SIZE];
    int   history_offset = 0;

    PerfTimer fps_timer;
    float     fps_display_value = 0.0f;
    float     frame_time_ms     = 0.0f;
    int       frame_counter     = 0;
    double    global_time       = 0.0;

    double physics_ms      = 0.0;
    double animation_ms    = 0.0;
    double scene_update_ms = 0.0;
    double bvh_build_ms    = 0.0;
    double shading_ms      = 0.0;
    double skybox_ms       = 0.0;
    double noise_ms        = 0.0;
    double display_flush_ms= 0.0;

    Vec3  current_player_pos, current_camera_dir, current_velocity;
    float cam_yaw = 0.0f, cam_pitch = 0.0f;
    bool  is_grounded    = false;
    int   triangle_count = 0;
    int   bvh_nodes = 0; int bvh_packed = 0;
    double update_ms = 0.0, render_ms = 0.0;
    double triangle_render_ms = 0.0, ui_render_ms = 0.0, bvh_intersection_ms = 0.0;
    size_t cpu_memory_usage  = 0;
    bool   global_ui_visible = true;

    const InputManager* input_ptr = nullptr;

    static void     SetCell(ImDrawState& ctx, int x, int y,
                            ConsoleGlyph c, const RGBColor& col, bool ui, bool border);
    static bool     IsClipped(int x, int y, const ImDrawState& s);
    static RGBColor FadeColor(const RGBColor& c, float alpha);
    void Begin(const char* title, int x, int y, int w, int h, const RGBColor& border_col);
    void End();
    void TextColored(const RGBColor& col, const char* fmt, ...);
    void Text(const char* fmt, ...);
    void DrawKeyValue(const char* label, const char* valStr, const RGBColor& valCol);
    void Value(const char* l, float v);
    void Value(const char* l, const Vec3& v);
    void Value(const char* l, int v);
    void Value(const char* l, const char* v);
    void Value(const char* l, const StrBuf<256>& v);
    void ValueBool(const char* l, bool v);
    void ValueBytes(const char* l, size_t bytes);
    void Separator();
    void ProgressBar(float f, int w, const RGBColor& c);
    void PlotLines(const char* label, const float* values, int count,
                   int offset, float min_scale, float max_scale, int height);
    void Dummy(int h);

    bool isMouseOver(int x, int y, int w, int h) const;
    bool MenuItem(const char* l);
    void MenuToggle(const char* l, bool* v);
    void MenuSlider(const char* l, float* v, float mn, float mx, float step);
    void MenuSlider(const char* l, int*   v, int   mn, int   mx, int   step);
    void MenuTextInput(const char* label, StrBuf<256>* value);
    void MenuAction(const char* l, bool* triggered);
    void DrawTabs(int w);
};