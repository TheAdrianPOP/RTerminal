#pragma once
#ifndef TWEEN_SYSTEM_H
#define TWEEN_SYSTEM_H

// Tween system — animate float / Vec2 / Color values over time.
// Integrates with easing.h for curves.
//
// Usage:
//   TweenID t = tweens.tween(&myFloat, 0.f, 10.f, 1.5f, Ease::outCubic);
//   tweens.update(dt);
//   tweens.onDone(t, []{ startNextAnim(); });

#include "../math/vector.h"
#include "../math/color.h"
#include "../math/easing.h"
#include "callback.h"

using TweenID = uint32_t;
static constexpr TweenID kInvalidTween = 0;

using EaseFn = float(*)(float);

template<typename T> T tweenLerp(T a, T b, float t);
template<> inline float tweenLerp<float>(float a, float b, float t) { return fast_lerp(a,b,t); }
template<> inline Vec2  tweenLerp<Vec2> (Vec2  a, Vec2  b, float t) { return a.lerp(b,t); }
template<> inline Color tweenLerp<Color>(Color a, Color b, float t) { return a.lerp(b,t); }

struct TweenBase {
    TweenID   id        = kInvalidTween;
    float     elapsed   = 0.f;
    float     duration  = 1.f;
    EaseFn    ease      = Ease::linear;
    bool      done      = false;
    bool      looping   = false;
    bool      pingpong  = false;
    VoidFn onDoneCb;

    virtual ~TweenBase() = default;
    virtual void apply(float t) = 0;

    void update(float dt) {
        if (done) return;
        elapsed += dt;
        float t = fast_clamp(elapsed / fast_max(duration, 1e-5f), 0.f, 1.f);
        if (pingpong) t = t < 0.5f ? t*2 : 2 - t*2;
        apply(ease(t));
        if (elapsed >= duration) {
            if (looping) { elapsed -= duration; }
            else { done = true; if (onDoneCb) onDoneCb(); }
        }
    }
};

template<typename T>
struct TweenT : TweenBase {
    T* target; T from, to;
    void apply(float t) override { if (target) *target = tweenLerp(from, to, t); }
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
        tweens_.push_back(tw);
        return tw->id;
    }

    // Convenience: tween from current value to 'to'
    template<typename T>
    TweenID tweenTo(T* target, T to, float duration, EaseFn ease = Ease::linear) {
        return tween(target, *target, to, duration, ease);
    }

    void setLoop(TweenID id, bool loop, bool pingpong=false) {
        if (auto* tw = find(id)) { tw->looping=loop; tw->pingpong=pingpong; }
    }
    void onDone(TweenID id, VoidFn cb) {
        if (auto* tw = find(id)) tw->onDoneCb = rt_move(cb);
    }
    void cancel(TweenID id)  { if (auto* tw=find(id)) tw->done=true; }
    void cancelAll()         { for(auto* tw:tweens_) delete tw; tweens_.clear(); }

    void update(float dt) {
        for (auto* tw : tweens_) tw->update(dt);
        // Remove finished non-looping tweens
        for (size_t i=0; i<tweens_.size(); ) {
            if (tweens_[i]->done && !tweens_[i]->looping) {
                delete tweens_[i];
                tweens_.erase(tweens_.begin()+i);
            } else ++i;
        }
    }

    size_t activeCount() const { return tweens_.size(); }
    ~TweenSystem() { cancelAll(); }

private:
    TweenBase* find(TweenID id) {
        for (auto* tw : tweens_) if (tw->id==id) return tw;
        return nullptr;
    }
    PodVector<TweenBase*> tweens_;
    TweenID nextId_ = 0;
};

inline TweenSystem& globalTweens() { static TweenSystem ts; return ts; }

#endif
