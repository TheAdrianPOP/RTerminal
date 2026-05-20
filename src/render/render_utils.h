#pragma once

#include "engine_config.h"
#include "console.h"
#include "str.h"

static const uint8_t bayer8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}
};

static FORCE_INLINE float getBayer8x8(int x, int y) {
    return (float)bayer8[y & 7][x & 7] / kBayerRange;
}

#define RT_RAMP_SLOTS kRampSlotCount

struct RampCache {
    StrBuf<256>             last_ramp_str;
    PodVector<ConsoleGlyph> glyphs;
};

extern RampCache* tl_ramp_slots;
void initRampCache();

static FORCE_INLINE RampCache& getRampCache(int slot_hint = 0) {
    if (!tl_ramp_slots) initRampCache();
    return tl_ramp_slots[(unsigned)slot_hint % RT_RAMP_SLOTS];
}
#define tl_ramp_cache (getRampCache(0))

inline void updateRampCache(const StrBuf<256>& ramp, int slot_hint = 0) {
    RampCache& cache = getRampCache(slot_hint);
    if (cache.last_ramp_str == ramp) return;
    cache.last_ramp_str = ramp;

    cache.glyphs.clear();
    cache.glyphs.reserve(ramp.size());

    size_t i = 0;
    while (i < (size_t)ramp.size()) {
        unsigned char c   = (unsigned char)ramp.data[i];
        int           len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (i + (size_t)len > (size_t)ramp.size()) break;
        char buf[kMaxGlyphBytes] = {0};
        for (int k = 0; k < len; ++k) buf[k] = ramp[i + k];
        cache.glyphs.emplace_back(buf);
        i += len;
    }
}

inline void prewarmRampCache(const StrBuf<256>& ramp) {
    for (int i = 0; i < kRampSlotCount; ++i)
        updateRampCache(ramp, i);
}