#include "app_base.h"
#include "display.h"
#include "input.h"
#include "bvh.h"
#include "ray_caster.h"
#include "threading.h"
#include "shading_provider.h"
#include "engine_config.h"
#include "math_utils.h"
#include "perf_timer.h"
#include "vector.h"
#include "triangle.h"
#include "console_utils.h"
#include "ui.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm> // For std::push_heap / std::pop_heap

// ---------------------------------------------------------------------------
// 1. Mandatory Engine Global
// ---------------------------------------------------------------------------
GlobalMetrics g_metrics;

// ---------------------------------------------------------------------------
// 2. Custom 3D Shading
// ---------------------------------------------------------------------------
class SnakeShader : public IShadingProvider {
public:
    ConsoleCell shadeHit(const Ray& ray, const HitRecord& hit, int screen_x, int screen_y) override {
        RGBColor color;
        if      (hit.triangle->material_id == 2) color = {80, 150, 200};      // Bounds
        else if (hit.triangle->material_id == 3) color = {50, 255, 50};       // Head
        else if (hit.triangle->material_id == 4) color = {20, 180, 20};       // Body
        else if (hit.triangle->material_id == 5) color = {255, 50, 50};       // Food
        else                                     color = {255, 0, 255};

        Vec3  light_dir = Vec3(0.5f, 1.0f, -0.4f).normalize();
        float diffuse   = fast_max(0.2f, hit.face_normal.dot(light_dir));
        return ConsoleCell(' ', {(int)(color.r * diffuse),
                                 (int)(color.g * diffuse),
                                 (int)(color.b * diffuse)}, false, false);
    }
    ConsoleCell shadeMiss(const Ray&) override {
        return ConsoleCell(' ', {10, 12, 16}, false, false);
    }
};

// ---------------------------------------------------------------------------
// 3. SnakeUI
// ---------------------------------------------------------------------------
class SnakeUI : public UI {
public:
    int   hud_score    = 0;
    int   hud_length   = 0;
    bool  hud_gameover = false;

    bool* ap_flag    = nullptr;
    float* cam_zoom  = nullptr;
    int*   speed_idx = nullptr; 

    void handleMenuInput(const InputManager& inp) {
        if (!menu.is_active) return;
        menu.nav_focus_tab_header = false; 
        if (inp.isKeyPressed(KEY_DOWN)) ++menu.selected_index;
        if (inp.isKeyPressed(KEY_UP) && menu.selected_index > 0) --menu.selected_index;
        if (menu.item_count > 0)
            menu.selected_index = fast_clamp(menu.selected_index, 0, menu.item_count - 1);
        menu.slider_edit_mode = true; 
    }

    void render(PodVector<PodVector<ConsoleCell>>& buffer, int w, int h) override {
        ctx.buffer   = &buffer;
        ctx.screen_w = w;
        ctx.screen_h = h;
        ctx.clip_x = 0; ctx.clip_y = 0; ctx.clip_w = w; ctx.clip_h = h;

        float ha = global_ui_visible ? menu.hud_anim_t : 0.0f;

        if (ha > 0.01f) {
            Begin("3D SNAKE", 1, 0, 28, 14, FadeColor(Colors::BORDER, ha));

            char buf[64];
            snprintf(buf, 64, "%d", hud_score);  DrawKeyValue("Score",  buf, FadeColor(Colors::OK,    ha));
            snprintf(buf, 64, "%d", hud_length); DrawKeyValue("Length", buf, FadeColor(Colors::VALUE, ha));
            if (ap_flag)
                DrawKeyValue("AI Pilot", *ap_flag ? "ON" : "OFF", FadeColor(*ap_flag ? Colors::OK : Colors::WARN, ha));
            if (speed_idx)
                DrawKeyValue("Speed", *speed_idx == 0 ? "INSTANT" : "NORMAL", FadeColor(Colors::VALUE, ha));
            
            Separator();
            TextColored(FadeColor(Colors::INACTIVE, ha), " FPS: %.0f   %.1f ms", fps_display_value, frame_time_ms);
            Separator();
            TextColored(FadeColor(Colors::STATUS_KEY, ha), " [P]   Toggle AI pilot");
            TextColored(FadeColor(Colors::STATUS_KEY, ha), " [M]   Open settings");
            TextColored(FadeColor(Colors::STATUS_KEY, ha), " [H]   Toggle HUD");
            TextColored(FadeColor(Colors::STATUS_KEY, ha), " [R]   Reset camera");
            TextColored(FadeColor(Colors::STATUS_KEY, ha), " [ESC] Quit");
            End();
        }

        if (ha > 0.01f && !hud_gameover && !menu.is_active) {
            const char* hint = "W/S/A/D: Steer   DRAG: Orbit   SCROLL: Zoom   MMB: Pan";
            int bw = (int)strlen(hint) + 4, bx = (w - bw) / 2, by = h - 4;
            if (bx >= 0 && by > 2) {
                Begin(nullptr, bx, by, bw, 3, FadeColor({50, 55, 75}, ha));
                TextColored(FadeColor(Colors::STATUS_KEY, ha), " %s", hint);
                End();
            }
        }

        if (hud_gameover) {
            int ow = 38, oh = 8, ox = (w - ow) / 2, oy = (h - oh) / 2;
            if (ox >= 0 && oy >= 0) {
                Begin("!! GAME OVER !!", ox, oy, ow, oh, Colors::ERR);
                Dummy(1);
                char sb[32], lb[32];
                snprintf(sb, 32, "%d", hud_score);  DrawKeyValue("Final Score",  sb, Colors::VALUE);
                snprintf(lb, 32, "%d", hud_length); DrawKeyValue("Snake Length", lb, Colors::VALUE);
                Separator();
                TextColored(Colors::OK,      "   SPACE / ENTER  - restart");
                TextColored(Colors::INACTIVE,"   ESC             - quit");
                End();
            }
        }

        if (menu.is_active) {
            menu.nav_focus_tab_header = false; 
            float ma  = menu.open_anim_t;
            int   mw  = 46, mh = 20, mx = (w - mw) / 2, my = (h - mh) / 2;
            if (mx >= 0 && my >= 0) {
                Begin("SETTINGS", mx, my, mw, mh, FadeColor(Colors::BORDER, ma));
                menu.item_count = 0; 

                TextColored(FadeColor(Colors::TITLE, ma), " Game");
                Separator();
                if (ap_flag) MenuToggle("AI Auto-Pilot", ap_flag);
                if (speed_idx) MenuSlider("Speed (0=INSTANT, 3=SLOW)", speed_idx, 0, 3, 1);

                Dummy(1);
                TextColored(FadeColor(Colors::TITLE, ma), " Camera");
                Separator();
                if (cam_zoom) MenuSlider("Zoom Distance", cam_zoom, 5.0f, 100.0f, 1.0f);

                Dummy(2);
                Separator();
                TextColored(FadeColor(Colors::INACTIVE, ma), " UP / DOWN    navigate");
                TextColored(FadeColor(Colors::INACTIVE, ma), " LEFT / RIGHT adjust");
                TextColored(FadeColor(Colors::INACTIVE, ma), " M / ESC      close");
                End();
            }
        }
    }
};

// ---------------------------------------------------------------------------
// 4. Main Game
// ---------------------------------------------------------------------------
// Custom A* Priority Queue Node
struct AStarNode {
    int id;
    int f_score;
    bool operator<(const AStarNode& other) const {
        return f_score > other.f_score; // Min-heap (smallest f_score at top)
    }
};

class SnakeGame : public AppBase {
    Display          display_;
    InputManager     input_;
    ParallelExecutor executor_;
    BVH              bvh_;
    SnakeShader      shader_;
    GraphicsConfig   gfx_;
    SnakeUI          ui_;

    PodVector<PodVector<ConsoleCell>> ui_buffer_;
    PodVector<Vec3>     snake_;
    PodVector<Triangle> scene_tris_;

    Vec3  dir_   {0,0,1}, up_   {0,1,0}, right_   {1,0,0};
    Vec3  next_dir_{0,0,1}, next_up_{0,1,0}, next_right_{1,0,0};
    Vec3  food_;

    float move_timer_    = 0.0f;
    bool  game_over_     = false;
    bool  input_queued_  = false;
    bool  auto_pilot_    = false;
    int   speed_idx_     = 3; // 0 = INSTANT
    int   score_         = 0;

    const int BOARD_SIZE = 25; 

    float cam_yaw_    = -45.0f;
    float cam_pitch_  =  20.0f;
    float cam_radius_ =  40.0f;
    Vec3  cam_target_ = {0, 0, 0};

    // A* Buffers
    PodVector<int>  g_score_;
    PodVector<int>  f_score_;
    PodVector<int>  a_parent_;
    PodVector<bool> a_visited_;
    PodVector<AStarNode> a_queue_;

    PerfTimer render_timer_;

public:
    // By passing a target FPS of 1000.0f, the AppBase loop limiter is effectively disabled
    SnakeGame() : AppBase(1000.0f), executor_(4) {
        int mn = (2*BOARD_SIZE+1)*(2*BOARD_SIZE+1)*(2*BOARD_SIZE+1);
        g_score_.assign(mn, 999999);
        f_score_.assign(mn, 999999);
        a_parent_.assign(mn, -1);
        a_visited_.assign(mn, false);
        a_queue_.reserve(mn);
    }

    bool onInit() override {
        gfx_.pixel_mode = true;
        gfx_.color_mode = ColorMode::TrueColor;

        ui_.bindInput(&input_);
        ui_.ap_flag   = &auto_pilot_;
        ui_.cam_zoom  = &cam_radius_;
        ui_.speed_idx = &speed_idx_;

        resetGame();
        return true;
    }

    void resetGame() {
        snake_.clear();
        snake_.push_back(Vec3(0,0,0));
        snake_.push_back(Vec3(0,0,-1));
        snake_.push_back(Vec3(0,0,-2));
        
        dir_=Vec3(0,0,1); up_=Vec3(0,1,0); right_=Vec3(1,0,0);
        next_dir_=dir_; next_up_=up_; next_right_=right_;
        
        input_queued_=false; game_over_=false; score_=0;
        spawnFood();
    }

    void resetCamera() {
        cam_yaw_=-45.0f; cam_pitch_=20.0f;
        cam_radius_=40.0f; cam_target_={0,0,0};
    }

    void spawnFood() {
        while (true) {
            int rx = (rand() % (BOARD_SIZE*2-2)) - (BOARD_SIZE-1);
            int ry = (rand() % (BOARD_SIZE*2-2)) - (BOARD_SIZE-1);
            int rz = (rand() % (BOARD_SIZE*2-2)) - (BOARD_SIZE-1);
            food_ = Vec3((float)rx,(float)ry,(float)rz);
            
            bool ok = true;
            for (int i = 0; i < (int)snake_.size(); ++i) {
                if (fast_abs(snake_[i].x-food_.x)<0.1f &&
                    fast_abs(snake_[i].y-food_.y)<0.1f &&
                    fast_abs(snake_[i].z-food_.z)<0.1f) { ok=false; break; }
            }
            if (ok) break;
        }
    }

    int flattenCoords(int x, int y, int z) {
        int w = 2*BOARD_SIZE+1;
        return (x+BOARD_SIZE) + (y+BOARD_SIZE)*w + (z+BOARD_SIZE)*w*w;
    }

    int manhattan(int x1, int y1, int z1, int x2, int y2, int z2) {
        return (int)(fast_abs((float)(x1 - x2)) + fast_abs((float)(y1 - y2)) + fast_abs((float)(z1 - z2)));
    }

    // A* Pathfinding implementation
    Vec3 getAIMove() {
        int mn = (2*BOARD_SIZE+1)*(2*BOARD_SIZE+1)*(2*BOARD_SIZE+1);
        g_score_.assign(mn, 999999);
        a_parent_.assign(mn, -1);
        a_visited_.assign(mn, false);
        a_queue_.clear();

        Vec3 head = snake_[0];
        int sx = (int)fast_round(head.x), sy = (int)fast_round(head.y), sz = (int)fast_round(head.z);
        int gx = (int)fast_round(food_.x), gy = (int)fast_round(food_.y), gz = (int)fast_round(food_.z);
        
        int sf = flattenCoords(sx, sy, sz);
        int gf = flattenCoords(gx, gy, gz);

        // Mark body as obstacles
        for (int i = 1; i < (int)snake_.size()-1; ++i) {
            int bx=(int)fast_round(snake_[i].x), by=(int)fast_round(snake_[i].y), bz=(int)fast_round(snake_[i].z);
            a_visited_[flattenCoords(bx,by,bz)] = true;
        }

        g_score_[sf] = 0;
        a_queue_.push_back({sf, manhattan(sx, sy, sz, gx, gy, gz)});
        std::push_heap(a_queue_.begin(), a_queue_.end());

        bool found = false;
        int dx[]={1,-1,0,0,0,0}, dy[]={0,0,1,-1,0,0}, dz[]={0,0,0,0,1,-1};

        while (!a_queue_.empty()) {
            std::pop_heap(a_queue_.begin(), a_queue_.end());
            AStarNode curr = a_queue_.back();
            a_queue_.pop_back();

            if (curr.id == gf) { found = true; break; }

            int W = 2*BOARD_SIZE+1;
            int cx = (curr.id % W) - BOARD_SIZE;
            int cy = ((curr.id / W) % W) - BOARD_SIZE;
            int cz = (curr.id / (W * W)) - BOARD_SIZE;

            for (int i=0; i<6; ++i) {
                int nx = cx + dx[i], ny = cy + dy[i], nz = cz + dz[i];
                if (nx < -BOARD_SIZE || nx > BOARD_SIZE || ny < -BOARD_SIZE || ny > BOARD_SIZE || nz < -BOARD_SIZE || nz > BOARD_SIZE) continue;
                
                int nf = flattenCoords(nx, ny, nz);
                if (a_visited_[nf]) continue;

                int tentative_g = g_score_[curr.id] + 1;
                
                if (tentative_g < g_score_[nf]) {
                    a_parent_[nf] = curr.id;
                    g_score_[nf]  = tentative_g;
                    int f = tentative_g + manhattan(nx, ny, nz, gx, gy, gz);
                    
                    a_queue_.push_back({nf, f});
                    std::push_heap(a_queue_.begin(), a_queue_.end());
                }
            }
        }

        if (found) {
            int cur = gf;
            while (a_parent_[cur] != sf && a_parent_[cur] != -1) cur = a_parent_[cur];
            int W = 2*BOARD_SIZE+1;
            int nx = (cur % W) - BOARD_SIZE, ny = ((cur / W) % W) - BOARD_SIZE, nz = (cur / (W * W)) - BOARD_SIZE;
            return Vec3((float)(nx-sx), (float)(ny-sy), (float)(nz-sz));
        }

        // Panic mode fallback (BFS expansion to any open tile)
        for (int i=0; i<6; ++i) {
            int nx=sx+dx[i], ny=sy+dy[i], nz=sz+dz[i];
            if (nx<-BOARD_SIZE||nx>BOARD_SIZE||ny<-BOARD_SIZE||ny>BOARD_SIZE||nz<-BOARD_SIZE||nz>BOARD_SIZE) continue;
            if (!a_visited_[flattenCoords(nx,ny,nz)]) return Vec3((float)dx[i],(float)dy[i],(float)dz[i]);
        }
        return dir_;
    }

    void addBox(Vec3 center, Vec3 size, uint32_t matId) {
        Vec3 h = size * 0.5f;
        Vec3 p[8] = {
            center+Vec3(-h.x,-h.y,-h.z), center+Vec3( h.x,-h.y,-h.z),
            center+Vec3( h.x, h.y,-h.z), center+Vec3(-h.x, h.y,-h.z),
            center+Vec3(-h.x,-h.y, h.z), center+Vec3( h.x,-h.y, h.z),
            center+Vec3( h.x, h.y, h.z), center+Vec3(-h.x, h.y, h.z)
        };
        int idx[36]={0,1,2,0,2,3,1,5,6,1,6,2,5,4,7,5,7,6,4,0,3,4,3,7,3,2,6,3,6,7,4,5,1,4,1,0};
        for (int i=0;i<36;i+=3)
            scene_tris_.push_back(Triangle(p[idx[i]],p[idx[i+1]],p[idx[i+2]],Vec2(0,0),Vec2(1,0),Vec2(0,1),matId));
    }

    void buildScene() {
        scene_tris_.clear();
        if (snake_.empty()) return;

        // Pre-allocate memory to prevent stuttering
        scene_tris_.reserve(500); 

        float B = (float)BOARD_SIZE + 0.5f;
        float thickness = 0.05f;
        float len = B * 2.0f;

        // 1. Boundary Beams (12 boxes total)
        addBox({0, -B, -B}, {len, thickness, thickness}, 2);
        addBox({0, -B,  B}, {len, thickness, thickness}, 2);
        addBox({-B, -B, 0}, {thickness, thickness, len}, 2);
        addBox({ B, -B, 0}, {thickness, thickness, len}, 2);
        addBox({0,  B, -B}, {len, thickness, thickness}, 2);
        addBox({0,  B,  B}, {len, thickness, thickness}, 2);
        addBox({-B,  B, 0}, {thickness, thickness, len}, 2);
        addBox({ B,  B, 0}, {thickness, thickness, len}, 2);
        addBox({-B,  0, -B}, {thickness, len, thickness}, 2);
        addBox({ B,  0, -B}, {thickness, len, thickness}, 2);
        addBox({-B,  0,  B}, {thickness, len, thickness}, 2);
        addBox({ B,  0,  B}, {thickness, len, thickness}, 2);

        // 2. Render Food
        if (!game_over_) addBox(food_, {0.6f, 0.6f, 0.6f}, 5);

        // 3. GREEDY SNAKE MERGING
        // This collapses straight lines of segments into single boxes
        size_t i = 0;
        while (i < snake_.size()) {
            Vec3 start = snake_[i];
            Vec3 end = start;
            size_t j = i + 1;

            if (j < snake_.size()) {
                Vec3 diff = snake_[j] - snake_[i];
                // If segments are exactly 1 unit apart, they are candidates for merging
                if (fast_abs(diff.x) + fast_abs(diff.y) + fast_abs(diff.z) < 1.1f) {
                    while (j < snake_.size()) {
                        Vec3 next_diff = snake_[j] - snake_[j-1];
                        // If the snake continues in the same direction, keep extending the box
                        if (next_diff.x == diff.x && next_diff.y == diff.y && next_diff.z == diff.z) {
                            end = snake_[j];
                            j++;
                        } else break;
                    }
                }
            }

            // Calculate geometry for the merged line
            Vec3 center = (start + end) * 0.5f;
            float sc = (i == 0) ? 0.95f : 0.80f; // Scale slightly for grid look
            Vec3 size = {
                fast_abs(end.x - start.x) + sc,
                fast_abs(end.y - start.y) + sc,
                fast_abs(end.z - start.z) + sc
            };

            addBox(center, size, (i == 0) ? 3 : 4);
            i = j; // Skip the segments we just merged
        }

        // 4. Build the acceleration structure with the massive reduction in triangles
        bvh_.build(scene_tris_);
    }

    void onFrame(float dt) override {
        if (width<=0||height<=0) return;
        render_timer_.start();
        input_.update();

        // ── UI Updates ──
        ui_.hud_score    = score_;
        ui_.hud_length   = (int)snake_.size();
        ui_.hud_gameover = game_over_;
        ui_.setRenderTime(render_timer_.getElapsedMs());
        ui_.update(dt, snake_.empty() ? Vec3() : snake_[0], dir_, current_fps, Vec3(), true, true);

        bool menu_open = ui_.isMenuOpen();
        if (input_.isKeyPressed(KEY_M)) {
            ui_.setMenuOpen(!menu_open);
            menu_open = ui_.isMenuOpen();
        }
        if (input_.isKeyPressed(KEY_H)) ui_.toggleVisibility();

        if (input_.isKeyPressed(KEY_ESC)) {
            if (menu_open) { ui_.setMenuOpen(false); menu_open = false; }
            else           exit(0);
        }

        ui_.handleMenuInput(input_);

        // ── Camera Update ──
        float wheel = input_.getMouseWheelDelta();
        if (wheel != 0.0f) cam_radius_ = fast_clamp(cam_radius_ - wheel*3.0f, 5.0f, 100.0f);

        if (input_.isMouseButtonDown(MOUSE_LEFT) || input_.isMouseButtonDown(MOUSE_RIGHT)) {
            Vec2 md = input_.getMouseDelta();
            cam_yaw_   += md.u;
            cam_pitch_  = fast_clamp(cam_pitch_ + md.v, -89.0f, 89.0f);
        }

        float pr = degToRad(cam_pitch_), yr = degToRad(cam_yaw_);
        Vec3 forward = Vec3(-fast_cos(pr)*fast_sin(yr), -fast_sin(pr), -fast_cos(pr)*fast_cos(yr)).normalize();
        Vec3 right   = Vec3(0,1,0).cross(forward).normalize();
        Vec3 up      = forward.cross(right).normalize();

        if (input_.isMouseButtonDown(MOUSE_MIDDLE)) {
            Vec2 md = input_.getMouseDelta();
            float ps = cam_radius_*0.0015f;
            cam_target_ = cam_target_ + right*(-md.u*ps) + up*(md.v*ps);
        }
        if (input_.isKeyPressed(KEY_R)) resetCamera();

        // ── Game Logic ──
        if (!menu_open) {
            if (input_.isKeyPressed(KEY_P)) {
                auto_pilot_ = !auto_pilot_;
                ui_.addMessage(auto_pilot_ ? "AI Pilot: ON" : "AI Pilot: OFF", {255, 200, 50});
            }

            if (game_over_) {
                if (input_.isKeyPressed(KEY_SPACE) || input_.isKeyPressed(KEY_ENTER)) resetGame();
            } else if (!auto_pilot_ && !input_queued_) {
                // RELATIVE STEERING LOGIC
                // W/S: Pitch Up/Down | A/D: Turn Left/Right
                if (input_.isKeyPressed(KEY_W) || input_.isKeyPressed(KEY_UP)) {
                    next_dir_ = up_;    next_up_ = -dir_;  next_right_ = right_; input_queued_ = true;
                } else if (input_.isKeyPressed(KEY_S) || input_.isKeyPressed(KEY_DOWN)) {
                    next_dir_ = -up_;   next_up_ = dir_;   next_right_ = right_; input_queued_ = true;
                } else if (input_.isKeyPressed(KEY_A) || input_.isKeyPressed(KEY_LEFT)) {
                    next_dir_ = -right_; next_right_ = dir_; next_up_ = up_;      input_queued_ = true;
                } else if (input_.isKeyPressed(KEY_D) || input_.isKeyPressed(KEY_RIGHT)) {
                    next_dir_ = right_;  next_right_ = -dir_; next_up_ = up_;     input_queued_ = true;
                }
            }
        }

        // ── 2. Game Logic Loop (Replace the 'while' loop inside onFrame) ──
        if (!game_over_ && !menu_open) {
            bool is_instant = (speed_idx_ == 0);
            float interval = (speed_idx_ == 1) ? 0.08f : (speed_idx_ == 2 ? 0.15f : 0.30f);
            bool ate = false;

            while (!game_over_ && !ate) {
                if (!is_instant) {
                    move_timer_ += dt;
                    if (move_timer_ < interval) break;
                    move_timer_ -= interval;
                }

                if (auto_pilot_) next_dir_ = getAIMove();

                // ── CRITICAL FIX: Basis Re-Alignment ──
                // If the direction changed (AI or Player), we must rebuild the 'up' and 'right' vectors
                // to keep them perpendicular to the new 'forward' direction.
                if (next_dir_.x != dir_.x || next_dir_.y != dir_.y || next_dir_.z != dir_.z) {
                    // Find a temporary vector that isn't the new direction
                    Vec3 temp = (fast_abs(next_dir_.y) < 0.9f) ? Vec3(0,1,0) : Vec3(1,0,0);
                    // Use Cross Products to build a perfect 90-degree right and up vector
                    right_ = next_dir_.cross(temp).normalize();
                    up_    = right_.cross(next_dir_).normalize();
                    dir_   = next_dir_;
                }
                
                input_queued_ = false; // Move performed, allow next input
                Vec3 head = snake_[0] + dir_;

                // Wall Collision
                if (head.x > BOARD_SIZE || head.x < -BOARD_SIZE || head.y > BOARD_SIZE || head.y < -BOARD_SIZE || head.z > BOARD_SIZE || head.z < -BOARD_SIZE) {
                    game_over_ = true; break;
                }

                // Self Collision
                bool eating = (fast_abs(head.x - food_.x) < 0.1f && fast_abs(head.y - food_.y) < 0.1f && fast_abs(head.z - food_.z) < 0.1f);
                for (size_t i = 0; i < (eating ? snake_.size() : snake_.size() - 1); ++i) {
                    if ((head - snake_[i]).magnitudeSq() < 0.01f) { game_over_ = true; break; }
                }

                if (!game_over_) {
                    snake_.push_back(Vec3());
                    for (int i = (int)snake_.size() - 1; i > 0; --i) snake_[i] = snake_[i - 1];
                    snake_[0] = head;
                    if (eating) { score_ += 10; spawnFood(); ate = true; } 
                    else snake_.pop_back();
                }
                if (!is_instant) break;
            }
        }

        // ── Rendering ──
        buildScene();
        display_.Resize(width, height);
        ConsoleCell* buf = display_.GetBuffer();

        Vec3 eye;
        eye.x = cam_target_.x + cam_radius_ * fast_cos(pr) * fast_sin(yr);
        eye.y = cam_target_.y + cam_radius_ * fast_sin(pr);
        eye.z = cam_target_.z + cam_radius_ * fast_cos(pr) * fast_cos(yr);

        float fov_scale = fast_tan(degToRad(gfx_.fov) * 0.5f);
        float aspect    = (width / 2.0f) / (float)height;

        executor_.run(height, [&](int y) {
            float ny = 1.0f - (2.0f*(y+0.5f)/height);
            for (int x=0;x<width;++x) {
                float nx = (2.0f*(x+0.5f)/width) - 1.0f;
                Vec3 rd  = (forward + right*(nx*aspect*fov_scale) + up*(ny*fov_scale)).normalize();
                buf[y*width+x] = RayCaster::castRay(bvh_, Ray(eye,rd), shader_, gfx_.render_dist, x, y);
            }
        });

        ui_.setRenderTime(render_timer_.getElapsedMs());

        // ── UI Overlay ──
        if ((int)ui_buffer_.size() != height) {
            ui_buffer_.resize(height);
            for (int y=0;y<height;++y) ui_buffer_[y].resize(width);
        }
        for (int y=0;y<height;++y) {
            if ((int)ui_buffer_[y].size() != width) ui_buffer_[y].resize(width);
            for (int x=0;x<width;++x) ui_buffer_[y][x] = ConsoleCell();
        }

        ui_.render(ui_buffer_, width, height);

        for (int y=0;y<height;++y)
            for (int x=0;x<width;++x)
                if (ui_buffer_[y][x].is_ui)
                    buf[y*width+x] = ui_buffer_[y][x];

        display_.Flush(width, height, gfx_);
    }
};

int main() {
    SnakeGame app;
    app.run();
    return 0;
}