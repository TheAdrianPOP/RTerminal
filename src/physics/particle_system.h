#pragma once

#include "engine_config.h"
#include "vector.h"
#include "math_utils.h"
#include "console.h"

struct Particle {
    Vec3     position;
    Vec3     velocity;
    float    life;
    float    max_life;
    RGBColor color;
    bool     active = false;
};

template<int MAX_PARTICLES>
struct ParticlePool {
    Particle particles[MAX_PARTICLES];
    int      count    = 0;
    bool spawn(Vec3 pos, Vec3 vel, float lifetime, RGBColor col) {
        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!particles[i].active) {
                particles[i] = { pos, vel, lifetime, lifetime, col, true };
                ++count;
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (int i = 0; i < MAX_PARTICLES; ++i) particles[i].active = false;
        count = 0;
    }

    int capacity() const { return MAX_PARTICLES; }
};

struct ParticleEmitter {
    Vec3     origin;
    Vec3     direction = kParticleDefaultDirection;
    float    spread    = kParticleDefaultSpread;
    float    speed_min = kParticleDefaultSpeedMin;
    float    speed_max = kParticleDefaultSpeedMax;
    float    life_min  = kParticleDefaultLifeMin;
    float    life_max  = kParticleDefaultLifeMax;
    RGBColor color     = kParticleDefaultColor;
    float    rate      = kParticleDefaultRate;
    bool     active    = true;

    float    _accumulator = 0.f;
    int tickSpawn(float dt) {
        if (!active) return 0;
        _accumulator += rate * dt;
        int n = (int)_accumulator;
        _accumulator -= (float)n;
        return n;
    }
};

struct ParticleRng {
    uint32_t state;
    explicit ParticleRng(uint32_t seed = kParticleRngDefaultSeed) : state(seed) {}

    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float nextFloat() { return (float)(next() & 0x7FFFFF) / (float)0x7FFFFF; }

    float nextRange(float lo, float hi) { return lo + nextFloat() * (hi - lo); }
};

template<int N>
inline void updateParticles(ParticlePool<N>& pool,
                             ParticleEmitter& emitter,
                             ParticleRng&     rng,
                             float            dt,
                             Vec3             gravity = Vec3(0.f, -4.f, 0.f))
{
    int alive = 0;
    for (int i = 0; i < N; ++i) {
        Particle& p = pool.particles[i];
        if (!p.active) continue;
        p.velocity += gravity * dt;
        p.position += p.velocity * dt;
        p.life     -= dt;
        if (p.life <= 0.f) { p.active = false; }
        else               { ++alive; }
    }
    pool.count = alive;

    int to_spawn = emitter.tickSpawn(dt);
    for (int i = 0; i < to_spawn; ++i) {
        float theta = rng.nextRange(-emitter.spread, emitter.spread);
        float phi   = rng.nextRange(0.f, TWO_PI);
        float st    = fast_sin(theta), ct = fast_cos(theta);
        float sp    = fast_sin(phi),   cp = fast_cos(phi);

        Vec3 up   = (fast_abs(emitter.direction.y) < kParticleUpThresh)
                        ? Vec3(0,1,0) : Vec3(1,0,0);
        Vec3 right = emitter.direction.cross(up).normalize();
        Vec3 fwd   = emitter.direction.normalize();
        Vec3 up2   = right.cross(fwd);

        Vec3 dir = (fwd * ct + right * (st * cp) + up2 * (st * sp)).normalize();
        float speed = rng.nextRange(emitter.speed_min, emitter.speed_max);
        float life  = rng.nextRange(emitter.life_min,  emitter.life_max);

        pool.spawn(emitter.origin, dir * speed, life, emitter.color);
    }
}

template<int N>
inline void renderParticles(const ParticlePool<N>& pool,
                             ConsoleCell*           buffer,
                             int width, int height,
                             Vec3 cam_pos,
                             Vec3 cam_fwd,
                             Vec3 cam_right,
                             Vec3 cam_up,
                             float fov_x,
                             ConsoleGlyph glyph = ConsoleGlyph('*'),
                             bool fade = true)
{
    if (!buffer || width <= 0 || height <= 0) return;

    const float half_w  = (float)width  * 0.5f;
    const float half_h  = (float)height * 0.5f;
    const float fov_y   = fov_x * ((float)height / (float)width);
    const float tan_x   = fast_tan(fov_x * 0.5f);
    const float tan_y   = fast_tan(fov_y * 0.5f);

    for (int i = 0; i < N; ++i) {
        const Particle& p = pool.particles[i];
        if (!p.active) continue;

        Vec3  rel   = p.position - cam_pos;
        float depth = rel.dot(cam_fwd);
        if (depth <= kParticleNearClip) continue;

        float rx = rel.dot(cam_right) / depth;
        float ry = rel.dot(cam_up)    / depth;

        if (fast_abs(rx) > tan_x || fast_abs(ry) > tan_y) continue;

        int sx = (int)(half_w + rx / tan_x * half_w);
        int sy = (int)(half_h - ry / tan_y * half_h);

        if (sx < 0 || sx >= width || sy < 0 || sy >= height) continue;

        RGBColor col = p.color;
        if (fade && p.max_life > 0.f) {
            float t = fast_clamp(p.life / p.max_life, 0.f, 1.f);
            col.r   = (int16_t)(col.r * t);
            col.g   = (int16_t)(col.g * t);
            col.b   = (int16_t)(col.b * t);
        }

        buffer[sy * width + sx] = ConsoleCell(glyph, col, false, false);
    }
}