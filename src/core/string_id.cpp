#include "string_id.h"
#include "runtime.h"

static inline uint32_t fnv1a_runtime(const char* s) {
    uint32_t h = 2166136261u;
    while (s && *s) {
        h = (h ^ static_cast<uint8_t>(*s++)) * 16777619u;
    }
    return h;
}

#if !defined(NDEBUG)

StringInternTable& StringInternTable::instance() {
    static StringInternTable t;
    return t;
}

uint32_t StringInternTable::intern(const char* s) {
    if (!s) return 0;
    uint32_t id = sidHash(fnv1a_runtime(s));
    for (int i = 0; i < entryCount; ++i) {
        if (entries[i].id == id) return id;
    }

    if (entryCount >= kMaxStrings) return id;
    int len = (int)strlen(s);
    if (poolOffset + len + 1 >= kMaxChars) return id;

    entries[entryCount] = {id, poolOffset};
    memcpy(pool + poolOffset, s, len + 1);
    poolOffset += len + 1;
    ++entryCount;
    return id;
}

const char* StringInternTable::lookup(uint32_t id) const {
    for (int i = 0; i < entryCount; ++i) {
        if (entries[i].id == id) return pool + entries[i].offset;
    }
    return "<unknown>";
}

uint32_t sidRuntime(const char* s) {
    return StringInternTable::instance().intern(s);
}

const char* sidLookup(uint32_t id) {
    return StringInternTable::instance().lookup(id);
}

#else

uint32_t sidRuntime(const char* s) {
    return s ? sidHash(fnv1a_runtime(s)) : 0;
}

#endif