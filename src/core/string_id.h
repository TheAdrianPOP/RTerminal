#pragma once
#ifndef STRING_ID_H
#define STRING_ID_H

// StringID: compile-time or runtime FNV-1a hash of a string.
// Use for fast string comparisons, HashMap keys, asset names.
// Optionally stores the source string in a global intern table for debugging.

#include "hashmap.h"
#include <cstring>
#include <cstdio>

// ─── Compile-time FNV-1a ─────────────────────────────────────────────────────
constexpr uint32_t fnv1a_ct(const char* s, uint32_t h = 2166136261u) {
    return *s ? fnv1a_ct(s+1, (h ^ (uint8_t)*s) * 16777619u) : h;
}
// Ensure IDs never collide with kHmEmpty/kHmTombstone
constexpr uint32_t sidHash(uint32_t h) { return h < 2u ? h + 0x10000u : h; }

using StringID = uint32_t;

// Literal: SID("player") — evaluated at compile time
#define SID(str) (sidHash(fnv1a_ct(str)))

// ─── Runtime intern table (optional, debug builds) ───────────────────────────
#if !defined(NDEBUG)
struct StringInternTable {
    static constexpr int kMaxStrings = 512;
    static constexpr int kMaxChars   = 16384;

    struct Entry { uint32_t id; int offset; };
    Entry    entries[kMaxStrings] = {};
    char     pool[kMaxChars]     = {};
    int      entryCount = 0;
    int      poolOffset = 0;

    static StringInternTable& instance() {
        static StringInternTable t; return t;
    }

    // Register a string → returns its ID (same as SID at runtime)
    uint32_t intern(const char* s) {
        uint32_t id = sidHash(fnv1a(s));
        // Check existing
        for (int i = 0; i < entryCount; ++i)
            if (entries[i].id == id) return id;
        // Add new
        if (entryCount >= kMaxStrings) return id; // table full, still return hash
        int len = (int)strlen(s);
        if (poolOffset + len + 1 >= kMaxChars) return id;
        entries[entryCount] = {id, poolOffset};
        memcpy(pool + poolOffset, s, len+1);
        poolOffset += len + 1;
        ++entryCount;
        return id;
    }

    // Look up source string by ID (debug only). Returns "<unknown>" if not found.
    const char* lookup(uint32_t id) const {
        for (int i = 0; i < entryCount; ++i)
            if (entries[i].id == id) return pool + entries[i].offset;
        return "<unknown>";
    }
};

// Runtime SID — also registers source string for debugging
inline uint32_t sidRuntime(const char* s) {
    return StringInternTable::instance().intern(s);
}
inline const char* sidLookup(uint32_t id) {
    return StringInternTable::instance().lookup(id);
}

#define SID_RT(str)  sidRuntime(str)
#define SID_DBG(id)  sidLookup(id)

#else // NDEBUG — no intern table overhead
inline uint32_t sidRuntime(const char* s) { return sidHash(fnv1a(s)); }
#define SID_RT(str)  sidRuntime(str)
#define SID_DBG(id)  "<strings disabled in release>"
#endif

#endif
