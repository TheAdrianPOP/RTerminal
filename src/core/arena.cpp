#include "arena.h"

static inline size_t arena_round_up_pow2(size_t x) {
    if (x <= 1) return 1;
    x--;
    for (size_t i = 1; i < sizeof(size_t) * 8; i *= 2) {
        x |= x >> i;
    }
    return x + 1;
}

LinearArena::LinearArena(size_t initial_capacity)
    : head_(new ArenaBlock(initial_capacity > 0 ? initial_capacity : 1024))
    , curr_(head_)
    , initial_cap_(initial_capacity > 0 ? initial_capacity : 1024)
{
}

LinearArena::~LinearArena() {
    freeChain(head_);
    head_ = nullptr;
    curr_ = nullptr;
}

LinearArena::LinearArena(LinearArena&& o) noexcept
    : head_(o.head_)
    , curr_(o.curr_)
    , initial_cap_(o.initial_cap_)
{
    o.head_ = nullptr;
    o.curr_ = nullptr;
    o.initial_cap_ = 0;
}

LinearArena& LinearArena::operator=(LinearArena&& o) noexcept {
    if (this != &o) {
        freeChain(head_);
        head_        = o.head_;
        curr_        = o.curr_;
        initial_cap_ = o.initial_cap_;
        o.head_      = nullptr;
        o.curr_      = nullptr;
        o.initial_cap_ = 0;
    }
    return *this;
}

void LinearArena::freeChain(ArenaBlock* block) {
    while (block) {
        ArenaBlock* next_block = block->next;
        delete block;
        block = next_block;
    }
}

void* LinearArena::alloc(size_t n, size_t alignment) {
    if (n == 0) return nullptr;
    
    if (alignment < 1) alignment = 1;
    alignment = arena_round_up_pow2(alignment);

    // Re-initialize if the arena was moved from or empty
    if (!curr_) {
        size_t cap = initial_cap_ > 0 ? initial_cap_ : 1024;
        head_ = new ArenaBlock(cap);
        curr_ = head_;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(curr_->data.get());
    uintptr_t curr_pos = base + curr_->offset;
    uintptr_t aligned_ptr = (curr_pos + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    size_t next_offset = (aligned_ptr - base) + n;

    // Check if the current block can fit the allocation without overflow
    if (next_offset > curr_->data.cap || next_offset < curr_->offset) {
        size_t max_padding = alignment - 1;
        if (n > SIZE_MAX - max_padding) {
            return nullptr; // Overflow guard
        }

        // Worst-case capacity required for a fresh block starting at offset 0
        size_t needed = n + max_padding;
        size_t next_cap = curr_->data.cap * 2;
        if (next_cap < needed) {
            next_cap = needed > (SIZE_MAX / 2) ? needed : needed * 2;
        }

        // Check if an existing chained block from a prior reset can be reused
        if (curr_->next && curr_->next->data.cap >= needed) {
            curr_ = curr_->next;
            curr_->offset = 0;
        } else {
            // Free any obsolete next blocks that are too small
            if (curr_->next) {
                freeChain(curr_->next);
                curr_->next = nullptr;
            }
            ArenaBlock* b = new ArenaBlock(next_cap);
            curr_->next = b;
            curr_ = b;
        }

        // Recalculate aligned address in the new block (guaranteed to fit)
        base = reinterpret_cast<uintptr_t>(curr_->data.get());
        aligned_ptr = (base + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        next_offset = (aligned_ptr - base) + n;
    }

    curr_->offset = next_offset;
    return reinterpret_cast<void*>(aligned_ptr);
}

void LinearArena::reset() {
    for (ArenaBlock* b = head_; b; b = b->next) {
        b->offset = 0;
    }
    curr_ = head_;
}

size_t LinearArena::getUsed() const {
    size_t total = 0;
    for (ArenaBlock* b = head_; b; b = b->next) {
        total += b->offset;
        if (b == curr_) break;
    }
    return total;
}

size_t LinearArena::getCapacity() const {
    size_t total = 0;
    for (ArenaBlock* b = head_; b; b = b->next) {
        total += b->data.cap;
    }
    return total;
}