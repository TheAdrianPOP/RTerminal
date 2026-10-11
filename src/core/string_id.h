#pragma once
#ifndef STRING_ID_H
#define STRING_ID_H

#include "internal.h"

// ── Compile-time FNV-1a ──────────────────────────────────────────────────────
constexpr uint32_t fnv1a_ct(const char* s, uint32_t h = 2166136261u) {
    return *s ? fnv1a_ct(s+1, (h ^ (uint8_t)*s) * 16777619u) : h;
}

constexpr uint32_t fnv1a_ic_ct(const char* s, uint32_t h = 2166136261u) {
    return *s ? fnv1a_ic_ct(s+1, (h ^ (uint8_t)((*s >= 'A' && *s <= 'Z') ? (*s + 32) : *s)) * 16777619u) : h;
}

constexpr uint32_t sidHash(uint32_t h) { return h < 2u ? h + 0x10000u : h; }

using StringID = uint32_t;

#define SID(str)    (sidHash(fnv1a_ct(str)))
#define SID_IC(str) (sidHash(fnv1a_ic_ct(str)))

// ── Runtime intern table ──────────────────────────────────────────────────────
#if !defined(NDEBUG)
struct StringInternTable {
    static constexpr int kMaxStrings = 512;
    static constexpr int kMaxChars   = 16384;

    struct Entry { uint32_t id; int offset; };
    Entry    entries[kMaxStrings] = {};
    char     pool[kMaxChars]      = {};
    int      entryCount = 0;
    int      poolOffset = 0;

    static StringInternTable& instance();
    uint32_t intern(const char* s);
    const char* lookup(uint32_t id) const;
};

uint32_t    sidRuntime(const char* s);
const char* sidLookup(uint32_t id);

#define SID_RT(str)  sidRuntime(str)
#define SID_DBG(id)  sidLookup(id)

#else
uint32_t sidRuntime(const char* s);
#define SID_RT(str)  sidRuntime(str)
#define SID_DBG(id)  "<strings disabled in release>"
#endif

#endif // STRING_ID_H