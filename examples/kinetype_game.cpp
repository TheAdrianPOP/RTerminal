#include "kinetype_game.h"
#include "../engine/core/runtime.h"
#include "../engine/platform/gamepad.h"
#include <ctime>

static uint32_t hash2D(int x, int y, uint32_t seed) {
    uint32_t h = seed ^ (uint32_t)x ^ ((uint32_t)y << 16);
    h = ((h >> 16) ^ h) * 0x45d9f3b; h = ((h >> 16) ^ h) * 0x45d9f3b; h = (h >> 16) ^ h;
    return h;
}

bool KinetypeGame::isWall(int x, int y) const {
    if (x > -5 && x < 5 && y > -5 && y < 5) return false;
    uint32_t h1 = hash2D(x, y, seed);
    uint32_t h2 = hash2D(x / 4, y / 4, seed + 1);
    uint32_t h3 = hash2D(x / 2, y / 2, seed + 2);
    bool blocks = (h2 % 5 == 0) && (h3 % 3 != 0);
    bool dots   = (h1 % 15 == 0);
    return blocks || dots;
}

struct ASNode { Vec2i pos; int g, h; int parent_idx; };
static int getH(Vec2i a, Vec2i b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

static bool isOccupied(Vec2i p, const PodVector<Vec2i>& body, const PodVector<KinetypeGame::Food>& foods, const PodVector<Enemy>& enemies, int target_idx, const StrBuf<64>& target_word, const KinetypeGame* g, bool is_enemy_pathing, Vec2i target_p) {
    if (g->isWall(p.x, p.y)) return true;
    if (p == target_p) return false;
    for (const auto& part : body) if (p == part) return true;
    if (!is_enemy_pathing) {
        for (const auto& f : foods) { if (p == f.pos) { if (target_idx >= target_word.length() || f.c != target_word[target_idx]) return true; } }
        for (const auto& e : enemies) { if (e.active && (int)e.pos.x == p.x && (int)e.pos.y == p.y) return true; }
    }
    return false;
}

static void findPath(Vec2i start, Vec2i end, const KinetypeGame* game, const PodVector<Vec2i>& body, const PodVector<KinetypeGame::Food>& foods, const PodVector<Enemy>& enemies, int target_idx, const StrBuf<64>& target_word, PodVector<Vec2i>& out_path, bool is_enemy_pathing = false) {
    out_path.clear(); if (start == end) return;
    PodVector<ASNode> open, closed; open.push_back({start, 0, getH(start, end), -1});
    int max_nodes = is_enemy_pathing ? 400 : 700;
    while (!open.empty() && closed.size() < (size_t)max_nodes) {
        int best_idx = 0; for (int i = 1; i < (int)open.size(); ++i) if (open[i].g + open[i].h < open[best_idx].g + open[best_idx].h) best_idx = i;
        ASNode current = open[best_idx]; open.erase(open.begin() + best_idx); closed.push_back(current);
        if (current.pos == end) { int curr_idx = (int)closed.size() - 1; while (curr_idx != -1) { out_path.push_back(closed[curr_idx].pos); curr_idx = closed[curr_idx].parent_idx; } return; }
        Vec2i dirs[] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
        for (auto d : dirs) {
            Vec2i next = current.pos + d; 
            if (!is_enemy_pathing && body.size() > 1 && next == body[0]) continue; 
            if (isOccupied(next, body, foods, enemies, target_idx, target_word, game, is_enemy_pathing, end)) continue;
            bool in_closed = false; for (const auto& n : closed) if (n.pos == next) { in_closed = true; break; } if (in_closed) continue;
            int new_g = current.g + 1; int found_open = -1; for (int i = 0; i < (int)open.size(); ++i) if (open[i].pos == next) { found_open = i; break; }
            if (found_open == -1) open.push_back({next, new_g, getH(next, end), (int)closed.size() - 1});
            else if (new_g < open[found_open].g) { open[found_open].g = new_g; open[found_open].parent_idx = (int)closed.size() - 1; }
        }
    }
}

static void generateProceduralWord(StrBuf<64>& out) {
    static const char* starts[] = {"BR", "CR", "DR", "FR", "GR", "PR", "ST", "TR", "B", "C", "D", "F", "G", "H", "J", "K", "L", "M", "N", "P", "Q", "R", "S", "T", "V", "W", "X", "Z"};
    static const char* mids[]   = {"AE", "AI", "AU", "EA", "EE", "EI", "EO", "IA", "IE", "IO", "OA", "OE", "OI", "OO", "OU", "UA", "UE", "UI", "A", "E", "I", "O", "U", "Y"};
    static const char* ends[]   = {"ND", "NG", "NK", "NT", "ST", "RD", "RT", "TH", "CH", "SH", "D", "G", "K", "L", "M", "N", "P", "R", "S", "T"};
    out.clear(); out += starts[std::rand() % (sizeof(starts)/sizeof(starts[0]))]; out += mids[std::rand() % (sizeof(mids)/sizeof(mids[0]))];
    if (std::rand() % 2 == 0) out += ends[std::rand() % (sizeof(ends)/sizeof(ends[0]))];
    else { out += starts[std::rand() % (sizeof(starts)/sizeof(starts[0]))]; out += mids[std::rand() % (sizeof(mids)/sizeof(mids[0]))]; out += ends[std::rand() % (sizeof(ends)/sizeof(ends[0]))]; }
}

KinetypeGame::KinetypeGame() : AppBase(60.0f) { std::srand((unsigned)std::time(nullptr)); }
bool KinetypeGame::onInit() { display.Resize(width, height); seed = (uint32_t)std::rand(); resetGame(); return true; }
void KinetypeGame::onResize(int w, int h) { display.Resize(w, h); }

void KinetypeGame::resetGame() {
    snake_body.clear(); snake_chars.clear(); foods.clear(); enemies.clear(); projectiles.clear();
    snake_body.push_back({0, 0}); snake_chars.push_back('@'); prev_head_pos = Vec2(0, 0);
    head_dir = {1, 0}; score = 0; ammo = 1; game_over = false; tick_timer = 0.0f; tick_rate = 0.12f; enemy_timer = 0.0f; camera_pos = Vec2(0, 0); bot_mode = false;
    generateProceduralWord(target_word); target_idx = 0;
    for (int i = 0; i < 150; ++i) spawnFood();
    for (int i = 0; i < 25; ++i)  spawnEnemy();
}

void KinetypeGame::spawnFood() {
    Food f; Vec2i center = Vec2i((int)camera_pos.x, (int)camera_pos.y);
    for (int attempts = 0; attempts < 100; ++attempts) { f.pos = { center.x + (std::rand() % 200 - 100), center.y + (std::rand() % 120 - 60) }; if (!isWall(f.pos.x, f.pos.y)) break; }
    if (std::rand() % 3 == 0 && target_idx < target_word.length()) f.c = target_word[target_idx];
    else f.c = 'A' + (std::rand() % 26);
    foods.push_back(f);
}

void KinetypeGame::spawnEnemy() {
    Enemy e; Vec2 center = camera_pos; float dist = 40.0f + (float)(std::rand() % 40); float angle = (float)(std::rand() % 360) * 0.0174f;
    e.pos = Vec2(center.x + std::cos(angle) * dist, center.y + std::sin(angle) * dist);
    e.vel = Vec2(0, 0); e.icon = '&'; e.active = true; e.chasing = false; e.wander_target = e.pos; enemies.push_back(e);
}

void KinetypeGame::fireProjectile() {
    if (ammo <= 0) return; ammo--;
    Projectile p; p.pos = Vec2((float)snake_body[0].x, (float)snake_body[0].y);
    p.vel = Vec2((float)head_dir.x, (float)head_dir.y) * 20.0f; p.active = true; projectiles.push_back(p);
}

static bool hasLineOfSight(Vec2 start, Vec2 end, const KinetypeGame* game, float max_dist) {
    Vec2 dir = end - start; float d = dir.length(); if (d > max_dist) return false;
    dir = dir.normalize(); for (float s = 0.5f; s < d; s += 0.5f) { Vec2 p = start + dir * s; if (game->isWall((int)p.x, (int)p.y)) return false; }
    return true;
}

void KinetypeGame::updateEnemies(float dt) {
    Vec2 p_pos_v = Vec2((float)snake_body[0].x + 0.5f, (float)snake_body[0].y + 0.5f);
    Vec2i p_pos_i = snake_body[0];
    for (auto& e : enemies) {
        if (!e.active) continue;
        for (int i = 1; i < (int)snake_body.size(); ++i) { if (std::abs(e.pos.x - (float)snake_body[i].x) < 0.6f && std::abs(e.pos.y - (float)snake_body[i].y) < 0.6f) { e.active = false; score += 100; break; } }
        if (!e.active) continue;
        if (hasLineOfSight(e.pos, p_pos_v, this, 40.0f)) e.chasing = true;
        else if (e.chasing && (std::rand() % 150 == 0)) e.chasing = false;
        e.path_timer -= dt;
        if (e.path_timer <= 0) {
            Vec2i target_tile = p_pos_i;
            if (!e.chasing) { for (int i=0; i<15; ++i) { Vec2 cand = e.pos + Vec2((float)(std::rand()%20 - 10), (float)(std::rand()%16 - 8)); if (!isWall((int)cand.x, (int)cand.y)) { e.wander_target = cand; break; } } target_tile = {(int)e.wander_target.x, (int)e.wander_target.y}; }
            findPath({(int)e.pos.x, (int)e.pos.y}, target_tile, this, snake_body, {}, {}, 0, "", e.path, true); 
            e.path_timer = 0.4f + (float)(std::rand() % 40) / 100.f;
        }
        Vec2 target = e.pos; if (!e.path.empty()) { Vec2i next_tile = e.path.back(); if (getH({(int)e.pos.x, (int)e.pos.y}, next_tile) == 0 && e.path.size() > 1) { e.path.pop_back(); next_tile = e.path.back(); } target = Vec2((float)next_tile.x + 0.5f, (float)next_tile.y + 0.5f); }
        Vec2 dir = (target - e.pos); if ((Vec2((float)p_pos_i.x, (float)p_pos_i.y) - e.pos).lengthSq() < 0.8f) { game_over = true; return; }
        if (dir.lengthSq() > 0.01f) { e.vel = dir.normalize() * (e.chasing ? 5.5f : 3.0f); e.pos = e.pos + e.vel * dt; }
        for (auto& other : enemies) { if (&other == &e || !other.active) continue; Vec2 diff = e.pos - other.pos; float odSq = diff.lengthSq(); if (odSq < 0.9f && odSq > 0.001f) e.pos = e.pos + diff.normalize() * (dt * 1.5f); }
    }
}

void KinetypeGame::updateProjectiles(float dt) {
    for (auto& p : projectiles) {
        if (!p.active) continue;
        p.pos = p.pos + p.vel * dt;
        if (isWall((int)p.pos.x, (int)p.pos.y)) { p.active = false; continue; }
        for (auto& e : enemies) { if (e.active && (e.pos - p.pos).lengthSq() < 1.5f) { e.active = false; p.active = false; score += 200; break; } }
    }
}

void KinetypeGame::updateSnake() {
    if (game_over) return;
    prev_head_pos = Vec2((float)snake_body[0].x, (float)snake_body[0].y);
    Vec2i next_head = snake_body[0] + head_dir;
    if (isWall(next_head.x, next_head.y)) { game_over = true; return; }
    for (const auto& part : snake_body) { if (next_head == part) { game_over = true; return; } }
    for (const auto& e : enemies) { if (e.active && (int)e.pos.x == next_head.x && (int)e.pos.y == next_head.y) { game_over = true; return; } }
    for (int i = 0; i < (int)foods.size(); ++i) {
        if (next_head == foods[i].pos) {
            char eaten_c = foods[i].c;
            if (target_idx < target_word.length() && eaten_c == target_word[target_idx]) {
                target_idx++; score += 50; snake_chars.push_back(eaten_c); snake_body.push_back(snake_body.back());
                if (target_idx == target_word.length()) { score += 500; ammo++; generateProceduralWord(target_word); target_idx = 0; tick_rate = fast_max(0.04f, tick_rate * 0.95f); }
            } else { game_over = true; return; }
            foods.erase(foods.begin() + i); spawnFood(); break;
        }
    }
    for (int i = (int)snake_body.size() - 1; i > 0; --i) snake_body[i] = snake_body[i - 1];
    snake_body[0] = next_head;
}

void KinetypeGame::onFrame(float dt) {
    input.update();
    auto& gp = globalGamepad();
    gp.update(true); // Explicitly process input
    const auto& gs = gp.state(0);

    if (game_over) {
        if (input.isKeyPressed(KEY_ENTER) || gs.isDown(GamepadButton::Start) || gs.isDown(GamepadButton::A)) {
            resetGame();
        }
    }
    else {
        if (input.isKeyPressed('T') || gs.isPressed(GamepadButton::Y)) bot_mode = !bot_mode;
        if (bot_mode) {
             int best_f = -1; int min_d = 999999;
             for (int i=0; i<(int)foods.size(); ++i) { if (foods[i].c == target_word[target_idx]) { int d = getH(snake_body[0], foods[i].pos); if (d < min_d) { min_d = d; best_f = i; } } }
             if (best_f != -1) {
                 PodVector<Vec2i> bp; findPath(snake_body[0], foods[best_f].pos, this, snake_body, foods, enemies, target_idx, target_word, bp);
                 if (bp.size() >= 2) head_dir = bp[bp.size()-2] - snake_body[0];
                 else { Vec2i ds[] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}}; for (auto d : ds) { if (!isOccupied(snake_body[0] + d, snake_body, foods, enemies, target_idx, target_word, this, false, {999,999})) { head_dir = d; break; } } }
                 for (const auto& e : enemies) { if (e.active && (Vec2((float)snake_body[0].x, (float)snake_body[0].y) - e.pos).lengthSq() < 64.0f) fireProjectile(); }
             }
        } else {
            if ((input.isKeyPressed(KEY_UP) || input.isKeyDown('W') || gs.isDown(GamepadButton::DPadUp)) && head_dir.y == 0) head_dir = {0, -1};
            if ((input.isKeyPressed(KEY_DOWN) || input.isKeyDown('S') || gs.isDown(GamepadButton::DPadDown)) && head_dir.y == 0) head_dir = {0, 1};
            if ((input.isKeyPressed(KEY_LEFT) || input.isKeyDown('A') || gs.isDown(GamepadButton::DPadLeft)) && head_dir.x == 0) head_dir = {-1, 0};
            if ((input.isKeyPressed(KEY_RIGHT) || input.isKeyDown('D') || gs.isDown(GamepadButton::DPadRight)) && head_dir.x == 0) head_dir = {1, 0};
            Vec2 stick = gs.leftStick();
            if (fast_abs(stick.x) > 0.4f && head_dir.x == 0) head_dir = {(int)(stick.x > 0 ? 1 : -1), 0};
            if (fast_abs(stick.y) > 0.4f && head_dir.y == 0) head_dir = {0, (int)(stick.y > 0 ? 1 : -1)};
        }
        if (input.isKeyPressed(KEY_SPACE) || gs.isPressed(GamepadButton::A) || gs.isPressed(GamepadButton::RBumper)) fireProjectile();
        tick_timer += dt; if (tick_timer >= tick_rate) { updateSnake(); tick_timer = 0.0f; }
        updateEnemies(dt); updateProjectiles(dt);
        enemy_timer += dt; if (enemy_timer > 4.0f) { spawnEnemy(); enemy_timer = 0.0f; }
        for (int i = 0; i < (int)foods.size(); ++i) { if ((Vec2((float)foods[i].pos.x, (float)foods[i].pos.y) - camera_pos).lengthSq() > 22500.0f) { foods.erase(foods.begin() + i); spawnFood(); break; } }
        float t = fast_clamp(tick_timer / tick_rate, 0.0f, 1.0f);
        Vec2 vh = prev_head_pos + (Vec2((float)snake_body[0].x, (float)snake_body[0].y) - prev_head_pos) * t;
        camera_pos = camera_pos + (vh - camera_pos) * (dt * 10.0f);
    }
    ConsoleCell* buf = display.GetBuffer(); std::memset(buf, 0, (size_t)(width * height) * sizeof(ConsoleCell));
    DrawCtx ctx(buf, width, height); draw(ctx);
    GraphicsConfig gfx; gfx.pixel_mode = false; display.Flush(width, height, gfx);
}

void KinetypeGame::draw(DrawCtx& ctx) {
    int cam_x = (int)camera_pos.x - width / 2, cam_y = (int)camera_pos.y - height / 2;
    for (int y = 0; y < height; ++y) {
        int wy = y + cam_y;
        for (int x = 0; x < width; ++x) {
            int wx = x + cam_x; if (isWall(wx, wy)) ctx.put(x, y, '#', {90, 90, 110}, {40, 40, 50});
            else if ((wx % 10 == 0) && (wy % 10 == 0)) ctx.put(x, y, '+', {35, 35, 40});
        }
    }
    for (const auto& f : foods) { int sx = f.pos.x - cam_x, sy = f.pos.y - cam_y; if (sx >= 0 && sx < width && sy >= 2 && sy < height) { RGBColor col = (target_idx < target_word.length() && f.c == target_word[target_idx]) ? RGBColor(100, 255, 100) : RGBColor(255, 100, 100); char c_str[2] = {f.c, '\0'}; ctx.drawText(sx, sy, c_str, col); } }
    for (const auto& e : enemies) { if (!e.active) continue; int sx = (int)e.pos.x - cam_x, sy = (int)e.pos.y - cam_y; if (sx >= 0 && sx < width && sy >= 2 && sy < height) { char c_str[2] = {e.icon, '\0'}; ctx.drawText(sx, sy, c_str, e.chasing ? RGBColor(255, 60, 60) : RGBColor(180, 120, 255)); } }
    for (const auto& p : projectiles) { if (!p.active) continue; int sx = (int)p.pos.x - cam_x, sy = (int)p.pos.y - cam_y; if (sx >= 0 && sx < width && sy >= 2 && sy < height) ctx.drawText(sx, sy, "*", {255, 255, 0}); }
    for (int i = 0; i < (int)snake_body.size(); ++i) { Vec2i p = snake_body[i]; int sx = p.x - cam_x, sy = p.y - cam_y; if (sx >= 0 && sx < width && sy >= 2 && sy < height) { char c[2] = { snake_chars[i], '\0' }; ctx.drawText(sx, sy, c, (i == 0) ? RGBColor(255, 255, 255) : RGBColor(110, 210, 255)); } }
    ctx.fillRect({0, 0, width, 5}, {25, 25, 35}); 
    char h[128]; wsprintfA(h, "SCORE: %d | AMMO: %d %s", score, ammo, bot_mode ? "[BOT MODE ACTIVE]" : ""); ctx.drawText(2, 0, h, bot_mode ? RGBColor(50, 255, 255) : RGBColor(255, 255, 120));
    ctx.drawText(2, 1, "TARGET: ", {180, 180, 180}); for (int i = 0; i < (int)target_word.length(); ++i) { RGBColor col = (i < target_idx) ? RGBColor(50, 255, 50) : RGBColor(130, 130, 130); char c[2] = { target_word[i], '\0' }; ctx.drawText(10 + i, 1, c, col); }
    ctx.drawText(2, 3, "MOVE: WASD/Arrows/D-Pad | SHOOT: Space/A-Button | BOT: T/Y-Button", {140, 140, 150});
    ctx.drawText(2, 4, "GOAL: Collect GREEN letters in order. AVOID wrong letters and enemies!", {120, 200, 120});
    if (globalGamepad().isConnected(0)) ctx.drawText(width - 25, 0, "[CONTROLLER CONNECTED]", {100, 255, 100});
    if (game_over) {  ctx.fillRect({width/2-15, height/2-3, 30, 7}, {40, 20, 20}); ctx.drawTextCenter(0, width, height / 2, "GAME OVER", {255, 50, 50}); ctx.drawTextCenter(0, width, height / 2 + 2, "Press ENTER or START/A to restart", {160, 160, 160}); }
}
