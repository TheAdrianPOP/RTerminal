#pragma once
#ifndef HASHMAP_H
#define HASHMAP_H

// Open-addressing HashMap with linear probing.
// Keys must be uint32_t (StringID, EntityID, asset hash, etc.).
// Values are POD or trivially movable T.
// Integrates with LinearArena for zero-heap-alloc use.

#include "arena.h"
#include "math_utils.h"
#include <cstring>
#include <cassert>
#include <cstdint>

// FNV-1a hash helpers
FORCE_INLINE uint32_t fnv1a(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}
FORCE_INLINE uint32_t fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static constexpr uint32_t kHmEmpty   = 0u;
static constexpr uint32_t kHmTombstone = 1u;

template<typename V>
class HashMap {
public:
    struct Entry { uint32_t key; V value; };

    explicit HashMap(size_t initial_cap = 16, LinearArena* arena = nullptr)
        : arena_(arena), entries_(nullptr), count_(0), cap_(0) {
        rehash(initial_cap);
    }

    ~HashMap() { if (entries_ && !arena_) delete[] entries_; }

    // Non-copyable
    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    // Insert or overwrite
    V& operator[](uint32_t key) {
        assert(key > kHmTombstone && "Keys 0 and 1 are reserved");
        if (count_ * 2 >= cap_) rehash(cap_ * 2);
        size_t i = probe(key);
        if (entries_[i].key != key) { entries_[i].key = key; ++count_; }
        return entries_[i].value;
    }

    // Returns nullptr if not found
    V* get(uint32_t key) {
        size_t i = find(key);
        return (i != npos) ? &entries_[i].value : nullptr;
    }
    const V* get(uint32_t key) const {
        size_t i = find(key);
        return (i != npos) ? &entries_[i].value : nullptr;
    }

    bool contains(uint32_t key) const { return find(key) != npos; }

    // Returns false if key wasn't present
    bool erase(uint32_t key) {
        size_t i = find(key);
        if (i == npos) return false;
        entries_[i].key = kHmTombstone;
        --count_;
        return true;
    }

    size_t size()     const { return count_; }
    size_t capacity() const { return cap_; }
    void   clear()          { std::memset(entries_, 0, cap_ * sizeof(Entry)); count_ = 0; }

    // Iteration — skips empty/tombstone slots
    template<typename Fn> void forEach(Fn&& fn) {
        for (size_t i = 0; i < cap_; ++i)
            if (entries_[i].key > kHmTombstone) fn(entries_[i].key, entries_[i].value);
    }

private:
    static constexpr size_t npos = ~(size_t)0;

    size_t find(uint32_t key) const {
        if (!cap_) return npos;
        size_t mask = cap_ - 1, i = key & mask;
        while (entries_[i].key != kHmEmpty) {
            if (entries_[i].key == key) return i;
            i = (i + 1) & mask;
        }
        return npos;
    }

    size_t probe(uint32_t key) {
        size_t mask = cap_ - 1, i = key & mask;
        while (entries_[i].key != kHmEmpty && entries_[i].key != kHmTombstone && entries_[i].key != key)
            i = (i + 1) & mask;
        return i;
    }

    void rehash(size_t new_cap) {
        // Round up to next power of 2
        size_t nc = 16;
        while (nc < new_cap) nc <<= 1;

        Entry* old = entries_; size_t old_cap = cap_;
        if (arena_) {
            entries_ = (Entry*)arena_->alloc(nc * sizeof(Entry), alignof(Entry));
        } else {
            entries_ = new Entry[nc];
        }
        std::memset(entries_, 0, nc * sizeof(Entry));
        cap_ = nc; count_ = 0;

        if (old) {
            for (size_t i = 0; i < old_cap; ++i)
                if (old[i].key > kHmTombstone) (*this)[old[i].key] = old[i].value;
            if (!arena_) delete[] old;
        }
    }

    LinearArena* arena_;
    Entry*       entries_;
    size_t       count_;
    size_t       cap_;
};

#endif
