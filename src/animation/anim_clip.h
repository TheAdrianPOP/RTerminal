#pragma once
#ifndef ANIM_CLIP_H
#define ANIM_CLIP_H

// AnimClip: a simple keyframe animation clip.
// Each clip is a sequence of frames; each frame holds a glyph + color to display.
// Useful for sprite-sheet-style terminal animations (walking character, explosions, etc.).
//
// For skeletal animation see geometry/skeleton.h — AnimStateMachine drives that.

#include "../platform/console.h"
#include "../core/string_id.h"
#include "../math/math_utils.h"

static constexpr int kMaxFramesPerClip = 64;
static constexpr int kMaxClipNameLen   = 32;

struct AnimFrame {
    ConsoleGlyph glyph;      // character to display this frame
    RGBColor     color;      // foreground color
    RGBColor     bgColor;    // background color (optional)
};

struct AnimClip {
    char       name[kMaxClipNameLen] = {};
    StringID   id     = 0;
    AnimFrame  frames[kMaxFramesPerClip] = {};
    int        frameCount = 0;
    float      fps        = 8.f;    // frames per second
    bool       looping    = true;

    void setName(const char* n) {
        strncpy(name, n, kMaxClipNameLen-1);
        id = SID_RT(n);
    }
    void addFrame(ConsoleGlyph g, RGBColor fg, RGBColor bg={}) {
        if (frameCount < kMaxFramesPerClip)
            frames[frameCount++] = {g, fg, bg};
    }
    float duration() const { return frameCount / fast_max(fps, 0.01f); }
};

// ── Animator: plays an AnimClip, tracks time, returns current frame ───────────

struct Animator {
    const AnimClip* clip    = nullptr;
    float           time    = 0.f;
    bool            playing = true;
    bool            done    = false;   // true once a non-looping clip finishes

    void play(const AnimClip* c, bool restart = true) {
        if (!c) return;
        clip    = c;
        playing = true;
        done    = false;
        if (restart) time = 0.f;
    }
    void pause()  { playing = false; }
    void resume() { playing = true; }
    void stop()   { playing = false; time = 0.f; done = false; }

    void update(float dt) {
        if (!clip || !playing || done) return;
        time += dt;
        float dur = clip->duration();
        if (time >= dur) {
            if (clip->looping) time -= dur * (int)(time/dur);
            else               { time = dur; done = true; playing = false; }
        }
    }

    // Current frame (safe — returns frame 0 if clip is null/empty)
    const AnimFrame& currentFrame() const {
        static AnimFrame kDefault{};
        if (!clip || clip->frameCount == 0) return kDefault;
        float t     = fast_clamp(time, 0.f, clip->duration());
        int   index = (int)(t * clip->fps);
        index = fast_clamp(index, 0, clip->frameCount-1);
        return clip->frames[index];
    }
    int currentFrameIndex() const {
        if (!clip||clip->frameCount==0) return 0;
        int i = (int)(time * clip->fps);
        return fast_clamp(i, 0, clip->frameCount-1);
    }
    float normalizedTime() const {
        if (!clip||clip->frameCount==0) return 0.f;
        return fast_clamp(time / clip->duration(), 0.f, 1.f);
    }
};

// ── AnimClipLibrary: store named clips ───────────────────────────────────────

static constexpr int kMaxLibraryClips = 64;

struct AnimClipLibrary {
    AnimClip clips[kMaxLibraryClips];
    int      count = 0;

    AnimClip* add(const char* name) {
        if (count >= kMaxLibraryClips) return nullptr;
        clips[count].setName(name);
        return &clips[count++];
    }
    AnimClip* find(StringID id) {
        for (int i=0; i<count; ++i) if (clips[i].id==id) return &clips[i];
        return nullptr;
    }
    AnimClip* find(const char* name) { return find(SID_RT(name)); }
};

#endif
