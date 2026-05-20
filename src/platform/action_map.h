#pragma once
#ifndef ACTION_MAP_H
#define ACTION_MAP_H

// Action map: bind logical actions ("jump", "attack") to one or more physical keys.
// Decouples game logic from specific key codes — remap at runtime without changing game code.

#include <initializer_list>

// ── GCC polyfill ──────────────────────────────────────────────────────────────
// GCC exposes __has_trivial_destructor as the builtin; __is_trivially_destructible
// is the MSVC/Clang spelling.  Map one to the other so callback.h and ring_buffer.h
// compile cleanly on GCC without pulling in <type_traits>.
//
// ORIGINAL (broken): used std::is_trivially_destructible_v<T> without
// including <type_traits>.  Replaced with the GCC builtin it was trying to wrap.
#if defined(__GNUC__) && !defined(__clang__)
#  ifndef __is_trivially_destructible
#    define __is_trivially_destructible(T) __has_trivial_destructor(T)
#  endif
#endif

#include "input.h"
#include "../core/string_id.h"
#include <cstdint>
#include <cstring>

static constexpr int kMaxBindingsPerAction = 4;
static constexpr int kMaxActions           = 64;

struct ActionBinding {
    StringID  id    = 0;
    int       keys[kMaxBindingsPerAction] = {-1,-1,-1,-1};
    int       mouse = -1;
    int       count = 0;

    void addKey(int k) { if (count < kMaxBindingsPerAction) keys[count++] = k; }
};

class ActionMap {
public:
    ActionMap() : count_(0) {}

    void bind(StringID action, int key) {
        ActionBinding& b = getOrCreate(action);
        b.addKey(key);
    }
    void bind(StringID action, std::initializer_list<int> keys) {
        ActionBinding& b = getOrCreate(action);
        for (int k : keys) b.addKey(k);
    }
    void bindMouse(StringID action, int mouseBtn) {
        getOrCreate(action).mouse = mouseBtn;
    }
    void unbind(StringID action) {
        for (int i = 0; i < count_; ++i) {
            if (bindings_[i].id == action) {
                bindings_[i] = bindings_[--count_]; return;
            }
        }
    }
    void clear() { count_ = 0; }

    bool isDown(StringID action, const InputManager& input) const {
        const ActionBinding* b = find(action);
        if (!b) return false;
        for (int i = 0; i < b->count; ++i)
            if (b->keys[i] >= 0 && input.isKeyDown(b->keys[i])) return true;
        if (b->mouse >= 0 && input.isMouseButtonDown(b->mouse)) return true;
        return false;
    }

    bool isPressed(StringID action, const InputManager& input) const {
        const ActionBinding* b = find(action);
        if (!b) return false;
        for (int i = 0; i < b->count; ++i)
            if (b->keys[i] >= 0 && input.isKeyPressed(b->keys[i])) return true;
        if (b->mouse >= 0 && input.isMouseButtonPressed(b->mouse)) return true;
        return false;
    }

    Vec2 axis2D(StringID left, StringID right, StringID up, StringID down, const InputManager& inp) const {
        Vec2 v{};
        if (isDown(left,  inp)) v.x -= 1.f;
        if (isDown(right, inp)) v.x += 1.f;
        if (isDown(up,    inp)) v.y -= 1.f;
        if (isDown(down,  inp)) v.y += 1.f;
        float l = v.length();
        return l > 1.f ? v * (1.f/l) : v;
    }

private:
    ActionBinding bindings_[kMaxActions];
    int           count_;

    ActionBinding& getOrCreate(StringID id) {
        for (int i = 0; i < count_; ++i) if (bindings_[i].id == id) return bindings_[i];
        if (count_ < kMaxActions) {
            bindings_[count_] = ActionBinding{}; bindings_[count_].id = id; return bindings_[count_++];
        }
        return bindings_[0];
    }
    const ActionBinding* find(StringID id) const {
        for (int i = 0; i < count_; ++i) if (bindings_[i].id == id) return &bindings_[i];
        return nullptr;
    }
};

#endif