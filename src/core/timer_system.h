#pragma once
#ifndef TIMER_SYSTEM_H
#define TIMER_SYSTEM_H

// Timer system — fire callbacks after a delay or repeatedly.
// Update with timerUpdate(dt) each frame. All times in seconds.

#include "math_utils.h"
#include "callback.h"
#include "../math/vector.h"
#include <cstdint>

using TimerID = uint32_t;
static constexpr TimerID kInvalidTimer = 0;

struct Timer {
    TimerID id       = kInvalidTimer;
    float   delay    = 0.f;    // time until next fire
    float   interval = 0.f;    // >0 = repeating
    int     repeat   = -1;     // -1 = infinite, 0 = expired, >0 = remaining
    bool    paused   = false;
    VoidFn  cb;
};

class TimerSystem {
public:
    // One-shot: fire once after 'delay' seconds
    TimerID after(float delay, VoidFn cb) {
        return add(delay, 0.f, 1, rt_move(cb));
    }

    // Repeating: fire every 'interval' seconds; count=-1 means forever
    TimerID every(float interval, VoidFn cb, int count = -1) {
        return add(interval, interval, count, rt_move(cb));
    }

    void cancel(TimerID id) {
        for (auto& t : timers_)
            if (t.id == id) { t.repeat = 0; return; }
    }
    void pause(TimerID id)  { for (auto& t : timers_) if (t.id==id) t.paused=true;  }
    void resume(TimerID id) { for (auto& t : timers_) if (t.id==id) t.paused=false; }
    void cancelAll()        { timers_.clear(); }

    // Call each frame with frame delta time in seconds
    void update(float dt) {
        for (auto& t : timers_) {
            if (t.repeat == 0 || t.paused) continue;
            t.delay -= dt;
            while (t.delay <= 0.f && t.repeat != 0) {
                if (t.cb) t.cb();
                if (t.repeat > 0) --t.repeat;
                if (t.repeat == 0) break;
                t.delay += (t.interval > 0.f ? t.interval : 1e9f);
            }
        }
        // Remove expired (manual loop — avoids std::remove_if)
        for (size_t i = 0; i < timers_.size(); ) {
            if (timers_[i].repeat == 0)
                timers_.erase(timers_.begin() + i);
            else
                ++i;
        }
    }

    size_t activeCount() const { return timers_.size(); }

private:
    TimerID add(float delay, float interval, int repeat, VoidFn cb) {
        TimerID id = ++nextId_;
        Timer t;
        t.id = id; t.delay = delay; t.interval = interval;
        t.repeat = repeat; t.paused = false; t.cb = rt_move(cb);
        timers_.push_back(rt_move(t));
        return id;
    }

    PodVector<Timer> timers_;
    TimerID nextId_ = 0;
};

// Global singleton timer system
inline TimerSystem& globalTimers() { static TimerSystem ts; return ts; }

#endif
