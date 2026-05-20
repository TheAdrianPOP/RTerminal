#pragma once
#ifndef ARENA_H
#define ARENA_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include "str.h"
#include "move.h"   // rt_forward — no <utility> needed

struct ArenaBlock {
    OwnedArr<uint8_t> data;
    size_t          offset;
    ArenaBlock*     next;

    ArenaBlock(size_t c) : data(c), offset(0), next(nullptr) {}
};

class LinearArena {
public:
    explicit LinearArena(size_t initial_capacity)
        : initial_cap_(initial_capacity)
    {
        head_ = new ArenaBlock(initial_capacity);
        curr_ = head_;
    }

    ~LinearArena() {
        while (head_) {
            ArenaBlock* next = head_->next;
            delete head_;
            head_ = next;
        }
    }

    void* alloc(size_t n, size_t alignment = 8) {
        size_t current_ptr = (size_t)(curr_->data.get() + curr_->offset);
        size_t padding = (alignment - (current_ptr & (alignment - 1))) & (alignment - 1);

        if (curr_->offset + padding + n <= curr_->data.cap) {
            void* ptr = curr_->data.get() + curr_->offset + padding;
            curr_->offset += padding + n;
            return ptr;
        }

        size_t next_cap = (n + padding > curr_->data.cap)
            ? (n + padding) * 2
            : curr_->data.cap * 2;
        ArenaBlock* b = new ArenaBlock(next_cap);
        curr_->next = b;
        curr_ = b;

        current_ptr = (size_t)(curr_->data.get() + curr_->offset);
        padding = (alignment - (current_ptr & (alignment - 1))) & (alignment - 1);
        void* ptr = curr_->data.get() + curr_->offset + padding;
        curr_->offset += padding + n;
        return ptr;
    }

    // ── FIX: was using std::forward (requires <utility>).
    //         Replaced with rt_forward from move.h, consistent with the rest
    //         of the codebase.
    template<typename T, typename... Args>
    T* emplace(Args&&... args) {
        void* ptr = alloc(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new (ptr) T(rt_forward<Args>(args)...);
    }

    void reset() {
        ArenaBlock* b = head_;
        while (b) { b->offset = 0; b = b->next; }
        curr_ = head_;
    }

    size_t getUsed() const {
        size_t total = 0;
        ArenaBlock* b = head_;
        while (b) {
            total += b->offset;
            if (b == curr_) break;
            b = b->next;
        }
        return total;
    }

    size_t getCapacity() const {
        size_t total = 0;
        ArenaBlock* b = head_;
        while (b) { total += b->data.cap; b = b->next; }
        return total;
    }

private:
    ArenaBlock* head_;
    ArenaBlock* curr_;
    size_t      initial_cap_;
};

#endif