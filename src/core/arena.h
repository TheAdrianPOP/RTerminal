#pragma once
#ifndef ARENA_H
#define ARENA_H

#include "internal.h"
#include "str.h"
#include "move.h"

struct ArenaBlock {
    OwnedArr<uint8_t> data;
    size_t            offset;
    ArenaBlock*       next;

    explicit ArenaBlock(size_t c)
        : data(c)
        , offset(0)
        , next(nullptr)
    {}
};

class LinearArena {
public:
    explicit LinearArena(size_t initial_capacity = 1024);
    ~LinearArena();

    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;

    LinearArena(LinearArena&& o) noexcept;
    LinearArena& operator=(LinearArena&& o) noexcept;

    void* alloc(size_t n, size_t alignment = 8);

    template<typename T, typename... Args>
    T* emplace(Args&&... args) {
        void* ptr = alloc(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new (ptr) T(rt_forward<Args>(args)...);
    }

    void reset();
    size_t getUsed() const;
    size_t getCapacity() const;

private:
    ArenaBlock* head_;
    ArenaBlock* curr_;
    size_t      initial_cap_;

    void freeChain(ArenaBlock* block);
};

#endif // ARENA_H