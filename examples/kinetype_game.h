#pragma once
#ifndef KINETYPE_GAME_H
#define KINETYPE_GAME_H

#include "../engine/core/app_base.h"
#include "../engine/platform/input.h"
#include "../engine/render/draw2d.h"
#include "../engine/render/display.h"
#include "../engine/math/vector.h"
#include "../engine/core/str.h"

struct Enemy {
    Vec2 pos;
    Vec2 vel;
    char icon;
    bool active = true;
    bool chasing = false;
    Vec2 wander_target;
    PodVector<Vec2i> path;
    float path_timer = 0.0f;
};

struct Projectile {
    Vec2 pos;
    Vec2 vel;
    bool active = true;
};

class KinetypeGame : public AppBase {
public:
    KinetypeGame();
    bool isWall(int x, int y) const;
    
    struct Food {
        Vec2i pos;
        char  c;
    };
    bool onInit()               override;
    void onFrame(float dt)      override;
    void onResize(int w, int h) override;

private:
    void resetGame();
    void spawnFood();
    void spawnEnemy();
    void fireProjectile();
    
    void updateSnake();
    void updateEnemies(float dt);
    void updateProjectiles(float dt);
    
    void draw(DrawCtx& ctx);

    uint32_t         seed = 0;
    PodVector<Vec2i> snake_body;
    PodVector<char>  snake_chars;
    Vec2i            head_dir;
    Vec2             prev_head_pos; // For visual smoothing
    
    PodVector<Food>       foods;
    PodVector<Enemy>      enemies;
    PodVector<Projectile> projectiles;
    
    StrBuf<64>       target_word;
    int              target_idx = 0;

    InputManager     input;
    Display          display;
    Vec2             camera_pos;
    
    float tick_timer   = 0.0f;
    float tick_rate    = 0.12f;
    float enemy_timer  = 0.0f;
    int   score        = 0;
    int   ammo         = 0;
    bool  game_over    = false;
    bool  bot_mode     = false;
};

#endif
