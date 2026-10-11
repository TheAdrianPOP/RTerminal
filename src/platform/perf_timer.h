#ifndef PERF_TIMER_H
#define PERF_TIMER_H

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include "atomic.h"
#include "engine_config.h"

class PerfTimer {
public:
    PerfTimer() {
#ifdef _WIN32
        QueryPerformanceFrequency(&frequency);
#endif
    }

    inline void start() {
#ifdef _WIN32
        QueryPerformanceCounter(&start_time);
#else
        clock_gettime(CLOCK_MONOTONIC, &start_time);
#endif
    }

    inline double getElapsedMs() const {
#ifdef _WIN32
        LARGE_INTEGER end_time;
        QueryPerformanceCounter(&end_time);
        return (double)(end_time.QuadPart - start_time.QuadPart) * kMsPerSecond / frequency.QuadPart;
#else
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        return (end_time.tv_sec  - start_time.tv_sec)  * kMsPerSecond +
               (end_time.tv_nsec - start_time.tv_nsec) / kNsToMs;
#endif
    }

private:
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER start_time;
#else
    struct timespec start_time;
#endif
};

class PerfProfiler {
public:
    inline void beginFrame() {
        timer.start();
        last_checkpoint = 0.0;
    }

    inline double checkpoint() {
        double current = timer.getElapsedMs();
        double delta = current - last_checkpoint;
        last_checkpoint = current;
        return delta;
    }

    inline double getTotal() const {
        return timer.getElapsedMs();
    }

private:
    PerfTimer timer;
    double last_checkpoint = 0.0;
};

struct GlobalMetrics {
    AtomicInt   skybox_pixels;
    AtomicInt   shaded_pixels;
    AtomicInt   triangle_tests;
    AtomicFloat bvh_traversal_ms;

    void reset() {
        skybox_pixels.store(0, rt_mo::relaxed);
        shaded_pixels.store(0, rt_mo::relaxed);
        triangle_tests.store(0, rt_mo::relaxed);
        bvh_traversal_ms.store(0.0f);
    }
    
    void addSkyboxPixels(int count) { skybox_pixels.fetch_add(count, rt_mo::relaxed); }
    void addShadedPixels(int count) { shaded_pixels.fetch_add(count, rt_mo::relaxed); }
    void addTriangleTests(int count) { triangle_tests.fetch_add(count, rt_mo::relaxed); }
    
    void addBvh(double ms) {
        bvh_traversal_ms.fetch_add((float)ms);
    }
};

extern GlobalMetrics g_metrics;

#endif