#pragma once
#ifndef HASHMAP_H
#define HASHMAP_H

// Open-addressing HashMap with linear probing.
// Keys must be uint32_t (StringID, EntityID, asset hash, etc.).
// Keys 0 (kHmEmpty) and 1 (kHmTombstone) are reserved.
// Values are POD or movable types.
// Integrates with LinearArena for zero-heap-alloc use.

#include "arena.h"
#include "math_utils.h"
#include "move.h"
#include "internal.h"

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

static constexpr uint32_t kHmEmpty     = 0u;
static constexpr uint32_t kHmTombstone = 1u;

template<typename V>
class HashMap {
public:
    struct Entry {
        uint32_t key;
        V        value;
    };

    explicit HashMap(size_t initial_cap = 16, LinearArena* arena = nullptr)
        : arena_(arena), entries_(nullptr), count_(0), occupied_(0), cap_(0) {
        rehash(initial_cap);
    }

    ~HashMap() {
        destroyEntries();
        if (entries_ && !arena_) {
            delete[] reinterpret_cast<uint8_t*>(entries_);
        }
    }

    // Non-copyable
    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    HashMap(HashMap&& o) noexcept
        : arena_(o.arena_)
        , entries_(o.entries_)
        , count_(o.count_)
        , occupied_(o.occupied_)
        , cap_(o.cap_) {
        o.entries_  = nullptr;
        o.count_    = 0;
        o.occupied_ = 0;
        o.cap_      = 0;
    }

    HashMap& operator=(HashMap&& o) noexcept {
        if (this != &o) {
            destroyEntries();
            if (entries_ && !arena_) {
                delete[] reinterpret_cast<uint8_t*>(entries_);
            }
            arena_    = o.arena_;
            entries_  = o.entries_;
            count_    = o.count_;
            occupied_ = o.occupied_;
            cap_      = o.cap_;
            o.entries_  = nullptr;
            o.count_    = 0;
            o.occupied_ = 0;
            o.cap_      = 0;
        }
        return *this;
    }

    // Insert or overwrite
    V& operator[](uint32_t key) {
        assert(key > kHmTombstone && "Keys 0 and 1 are reserved");
        if (occupied_ * 2 >= cap_) {
            rehash(cap_ * 2);
        }

        size_t mask = cap_ - 1;
        size_t i = key & mask;
        size_t tombstone_idx = npos;

        while (entries_[i].key != kHmEmpty) {
            if (entries_[i].key == key) {
                return entries_[i].value;
            }
            if (entries_[i].key == kHmTombstone && tombstone_idx == npos) {
                tombstone_idx = i;
            }
            i = (i + 1) & mask;
        }

        // Key does not exist. Insert at first tombstone if one was encountered, else at the empty slot.
        size_t insert_idx = (tombstone_idx != npos) ? tombstone_idx : i;
        if (tombstone_idx == npos) {
            ++occupied_;
        }

        entries_[insert_idx].key = key;
        new (&entries_[insert_idx].value) V();
        ++count_;
        return entries_[insert_idx].value;
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

    bool contains(uint32_t key) const {
        return find(key) != npos;
    }

    // Returns false if key wasn't present
    bool erase(uint32_t key) {
        size_t i = find(key);
        if (i == npos) return false;

        entries_[i].key = kHmTombstone;
        if constexpr (!__is_trivially_destructible(V)) {
            entries_[i].value.~V();
        }
        --count_;
        return true;
    }

    size_t size()     const { return count_; }
    size_t capacity() const { return cap_; }

    void clear() {
        destroyEntries();
        for (size_t i = 0; i < cap_; ++i) {
            entries_[i].key = kHmEmpty;
        }
        count_    = 0;
        occupied_ = 0;
    }

    // Iteration — skips empty/tombstone slots
    template<typename Fn>
    void forEach(Fn&& fn) {
        for (size_t i = 0; i < cap_; ++i) {
            if (entries_[i].key > kHmTombstone) {
                fn(entries_[i].key, entries_[i].value);
            }
        }
    }

    template<typename Fn>
    void forEach(Fn&& fn) const {
        for (size_t i = 0; i < cap_; ++i) {
            if (entries_[i].key > kHmTombstone) {
                fn(entries_[i].key, entries_[i].value);
            }
        }
    }

private:
    static constexpr size_t npos = ~(size_t)0;

    size_t find(uint32_t key) const {
        if (!cap_ || key <= kHmTombstone) return npos;
        size_t mask = cap_ - 1;
        size_t i = key & mask;
        while (entries_[i].key != kHmEmpty) {
            if (entries_[i].key == key) return i;
            i = (i + 1) & mask;
        }
        return npos;
    }

    void destroyEntries() {
        if constexpr (!__is_trivially_destructible(V)) {
            if (entries_) {
                for (size_t i = 0; i < cap_; ++i) {
                    if (entries_[i].key > kHmTombstone) {
                        entries_[i].value.~V();
                    }
                }
            }
        }
    }

    void rehash(size_t new_cap) {
        size_t nc = 16;
        while (nc < new_cap) nc <<= 1;

        Entry* old = entries_;
        size_t old_cap = cap_;

        if (arena_) {
            entries_ = (Entry*)arena_->alloc(nc * sizeof(Entry), alignof(Entry));
        } else {
            entries_ = reinterpret_cast<Entry*>(new uint8_t[nc * sizeof(Entry)]);
        }

        for (size_t i = 0; i < nc; ++i) {
            entries_[i].key = kHmEmpty;
        }

        cap_      = nc;
        count_    = 0;
        occupied_ = 0;

        if (old) {
            for (size_t i = 0; i < old_cap; ++i) {
                if (old[i].key > kHmTombstone) {
                    size_t mask = cap_ - 1;
                    size_t idx = old[i].key & mask;
                    while (entries_[idx].key != kHmEmpty) {
                        idx = (idx + 1) & mask;
                    }
                    entries_[idx].key = old[i].key;
                    new (&entries_[idx].value) V(rt_move(old[i].value));
                    if constexpr (!__is_trivially_destructible(V)) {
                        old[i].value.~V();
                    }
                    ++count_;
                    ++occupied_;
                }
            }
            if (!arena_) {
                delete[] reinterpret_cast<uint8_t*>(old);
            }
        }
    }

    LinearArena* arena_;
    Entry*       entries_;
    size_t       count_;
    size_t       occupied_;
    size_t       cap_;
};

#endif