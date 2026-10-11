#pragma once
#ifndef TWEEN_SYSTEM_H
#define TWEEN_SYSTEM_H

// Tween system — animate float / Vec2 / Color values over time.
// Integrates with easing.h for curves.

#include "vector.h"
#include "color.h"
#include "easing.h"
#include "callback.h"
#include "move.h"
#include "internal.h"

using TweenID = uint32_t;
static constexpr TweenID kInvalidTween = 0;

using EaseFn = float(*)(float);

template<typename T> T tweenLerp(T a, T b, float t);
template<> inline float tweenLerp<float>(float a, float b, float t) { return fast_lerp(a, b, t); }
template<> inline Vec2  tweenLerp<Vec2> (Vec2  a, Vec2  b, float t) { return a.lerp(b, t); }
template<> inline Color tweenLerp<Color>(Color a, Color b, float t) { return a.lerp(b, t); }

struct TweenBase {
    TweenID  id       = kInvalidTween;
    float    elapsed  = 0.f;
    float    duration = 1.f;
    EaseFn   ease     = Ease::linear;
    bool     done     = false;
    bool     looping  = false;
    bool     pingpong = false;
    VoidFn   onDoneCb;

    virtual ~TweenBase() = default;
    virtual void apply(float t) = 0;

    void update(float dt) {
        if (done) return;
        elapsed += dt;
        float progress = fast_clamp(elapsed / fast_max(duration, 1e-5f), 0.f, 1.f);
        float t = progress;
        if (pingpong) t = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
        apply(ease ? ease(t) : t);

        if (elapsed >= duration) {
            if (looping) {
                elapsed -= duration;
            } else {
                done = true;
                if (onDoneCb) onDoneCb();
            }
        }
    }
};

template<typename T>
struct TweenT : TweenBase {
    T* target = nullptr;
    T  from{};
    T  to{};

    void apply(float t) override {
        if (target) *target = tweenLerp(from, to, t);
    }
};

class TweenSystem {
public:
    template<typename T>
    TweenID tween(T* target, T from, T to, float duration, EaseFn ease = Ease::linear) {
        auto* tw     = new TweenT<T>();
        tw->id       = ++nextId_;
        tw->target   = target;
        tw->from     = from;
        tw->to       = to;
        tw->duration = duration;
        tw->ease     = ease;

        if (is_updating_) {
            pending_added_.push_back(tw);
        } else {
            tweens_.push_back(tw);
        }
        return tw->id;
    }

    template<typename T>
    TweenID tweenTo(T* target, T to, float duration, EaseFn ease = Ease::linear) {
        return tween(target, target ? *target : to, to, duration, ease);
    }

    void setLoop(TweenID id, bool loop, bool pingpong = false) {
        if (auto* tw = find(id)) {
            tw->looping  = loop;
            tw->pingpong = pingpong;
        }
    }

    void onDone(TweenID id, VoidFn cb) {
        if (auto* tw = find(id)) {
            tw->onDoneCb = rt_move(cb);
        }
    }

    void cancel(TweenID id) {
        if (auto* tw = find(id)) tw->done = true;
    }

    void cancelAll() {
        for (auto* tw : tweens_)        delete tw;
        for (auto* tw : pending_added_) delete tw;
        tweens_.clear();
        pending_added_.clear();
    }

    void update(float dt) {
        is_updating_ = true;
        size_t n = tweens_.size();
        for (size_t i = 0; i < n; ++i) {
            if (tweens_[i] && !tweens_[i]->done) {
                tweens_[i]->update(dt);
            }
        }
        is_updating_ = false;

        // Merge tweens registered during callbacks
        for (size_t i = 0; i < pending_added_.size(); ++i) {
            tweens_.push_back(pending_added_[i]);
        }
        pending_added_.clear();

        // Clean up completed tweens
        for (size_t i = 0; i < tweens_.size(); ) {
            if (tweens_[i]->done && !tweens_[i]->looping) {
                delete tweens_[i];
                tweens_.erase(tweens_.begin() + i);
            } else {
                ++i;
            }
        }
    }

    size_t activeCount() const { return tweens_.size() + pending_added_.size(); }
    ~TweenSystem() { cancelAll(); }

private:
    TweenBase* find(TweenID id) {
        for (auto* tw : tweens_)        if (tw->id == id) return tw;
        for (auto* tw : pending_added_) if (tw->id == id) return tw;
        return nullptr;
    }

    PodVector<TweenBase*> tweens_;
    PodVector<TweenBase*> pending_added_;
    TweenID               nextId_      = 0;
    bool                  is_updating_ = false;
};

inline TweenSystem& globalTweens() { static TweenSystem ts; return ts; }

#endif // TWEEN_SYSTEM_H