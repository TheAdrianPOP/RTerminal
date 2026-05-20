#pragma once
#ifndef CAMERA2D_H
#define CAMERA2D_H

// 2D camera: pan, zoom, world↔screen transform.
// "World" units are floats; "screen" units are integer terminal cells (columns, rows).

#include "../math/vector.h"
#include "../math/rect.h"
#include "../math/math_utils.h"

struct Camera2D {
    Vec2  position   = {0, 0};  // world-space center the camera is looking at
    float zoom       = 1.f;     // >1 = zoom in (larger world objects on screen)
    float rotation   = 0.f;     // radians (for tilted cameras; 0 = upright)
    int   screenW    = 80;      // terminal width in columns
    int   screenH    = 24;      // terminal height in rows

    // ── Transforms ───────────────────────────────────────────────────────────

    // World → screen (returns fractional — floor/round for pixel addressing)
    Vec2 worldToScreen(Vec2 world) const {
        Vec2 delta = (world - position) * zoom;
        if (rotation != 0.f) {
            float c = fast_cos(-rotation), s = fast_sin(-rotation);
            delta = {delta.x*c - delta.y*s, delta.x*s + delta.y*c};
        }
        return Vec2{delta.x + screenW * 0.5f, delta.y + screenH * 0.5f};
    }

    // Screen → world
    Vec2 screenToWorld(Vec2 screen) const {
        Vec2 delta = {screen.x - screenW*0.5f, screen.y - screenH*0.5f};
        if (rotation != 0.f) {
            float c = fast_cos(rotation), s = fast_sin(rotation);
            delta = {delta.x*c - delta.y*s, delta.x*s + delta.y*c};
        }
        return position + delta * (1.f / fast_max(zoom, 1e-5f));
    }

    Vec2i worldToCell(Vec2 world) const {
        Vec2 s = worldToScreen(world);
        return {(int)s.x, (int)s.y};
    }

    // ── Viewport ─────────────────────────────────────────────────────────────

    // Visible world-space rectangle (approximate — axis-aligned, ignores rotation)
    Rect worldViewport() const {
        float hw = screenW * 0.5f / zoom;
        float hh = screenH * 0.5f / zoom;
        return {position.x - hw, position.y - hh, hw*2, hh*2};
    }

    bool isVisible(Vec2 worldPos, float radius = 0.f) const {
        Rect vp = worldViewport();
        return vp.expand(radius).contains(worldPos);
    }
    bool isVisible(Rect worldRect) const {
        return worldViewport().overlaps(worldRect);
    }

    // ── Controls ─────────────────────────────────────────────────────────────

    void pan(Vec2 worldDelta)     { position += worldDelta; }
    void panScreen(Vec2 screenDelta) { position += screenDelta * (1.f/zoom); }
    void zoomAt(Vec2 worldAnchor, float factor) {
        position = worldAnchor + (position - worldAnchor) * (1.f/factor);
        zoom *= factor;
        zoom  = fast_max(zoom, 0.01f);
    }
    void setZoom(float z)         { zoom = fast_max(z, 0.01f); }
    void follow(Vec2 target, float lerp_t = 1.f) {
        position = position.lerp(target, fast_clamp(lerp_t, 0.f, 1.f));
    }
    void snapTo(Vec2 target)      { position = target; }
};

#endif
