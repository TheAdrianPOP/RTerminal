#pragma once
#ifndef APP_BASE_H
#define APP_BASE_H

#include "console_utils.h"
#include "engine_config.h"
#include "perf_timer.h"
#include "internal.h"

class AppBase {
public:
    explicit AppBase(float target_fps   = kDefaultTargetFps,
                     float max_dt_scale = kDefaultMaxDtScale)
        : target_fps_(target_fps > 0.f ? target_fps : 60.f)
        , max_dt_scale_(max_dt_scale > 0.f ? max_dt_scale : 5.f)
    {}

    virtual ~AppBase() = default;

    void run() {
        s_exit_requested_ = 0;
        signal(SIGINT, &AppBase::handleSigint);

        setupConsoleMode();
        getConsoleSize(width, height);
        if (width <= 0)  width = MIN_CONSOLE_WIDTH;
        if (height <= 0) height = MIN_CONSOLE_HEIGHT;

        if (!onInit()) {
            restoreConsoleMode();
            clearConsole();
            return;
        }

        const float targetFrameTime = 1.0f / target_fps_;
        const float maxDt           = targetFrameTime * max_dt_scale_;

        PerfTimer frame_clock;
        frame_clock.start();

        while (!s_exit_requested_) {
            float dt = static_cast<float>(frame_clock.getElapsedMs() * kMsToSeconds);
            frame_clock.start();

            if (dt > maxDt) dt = maxDt;
            if (dt <= 0.0f) dt = 1e-4f; // Prevent zero or negative dt

            fps_timer_ += dt;
            fps_counter_++;
            if (fps_timer_ >= 0.5f) {
                current_fps  = (fps_timer_ > 0.0f) ? (static_cast<float>(fps_counter_) / fps_timer_) : target_fps_;
                fps_counter_ = 0;
                fps_timer_   = 0.0f;
            }

            int nw = width, nh = height;
            getConsoleSize(nw, nh);
            if (nw != width || nh != height) {
                width  = nw;
                height = nh;
                onResize(width, height);
            }

            onFrame(dt);

            double elapsed_s = frame_clock.getElapsedMs() * kMsToSeconds;
            double sleep_s   = static_cast<double>(targetFrameTime) - elapsed_s;
            if (sleep_s > 0.0) {
                platform_sleep(sleep_s);
            }
        }

        restoreConsoleMode();
        clearConsole();
    }

protected:
    int   width       = 0;
    int   height      = 0;
    float current_fps = 0.f;

    virtual bool onInit()           = 0;
    virtual void onFrame(float dt)  = 0;
    virtual void onResize(int, int) {}

private:
    float target_fps_   = kDefaultTargetFps;
    float max_dt_scale_ = kDefaultMaxDtScale;
    int   fps_counter_  = 0;
    float fps_timer_    = 0.f;

    // Defined cleanly inline in C++17
    inline static volatile sig_atomic_t s_exit_requested_ = 0;
    static void handleSigint(int) { s_exit_requested_ = 1; }
};

#endif // APP_BASE_H