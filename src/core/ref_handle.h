#pragma once
#ifndef REF_HANDLE_H
#define REF_HANDLE_H

// RefHandle<T> — lightweight non-intrusive reference-counted smart pointer.
// Replaces std::shared_ptr without pulling in <memory>.
// Single-threaded only (matches the AssetManager's threading model).
// RefHandle<void> is the type-erased form used for heterogeneous storage.

#include "move.h"
#include <cstdlib>

// ── Control block ─────────────────────────────────────────────────────────────
struct RefBlock {
    int    refs;
    void (*destroy)(void*);
    void*  ptr;
};

inline RefBlock* rb_alloc(void* ptr, void(*destroy)(void*)) {
    RefBlock* b = static_cast<RefBlock*>(malloc(sizeof(RefBlock)));
    b->refs = 1; b->ptr = ptr; b->destroy = destroy;
    return b;
}
inline void rb_retain (RefBlock* b) { ++b->refs; }
inline void rb_release(RefBlock* b) {
    if (--b->refs == 0) { b->destroy(b->ptr); free(b); }
}

// ── Typed strong handle ───────────────────────────────────────────────────────
template<typename T>
class RefHandle {
public:
    RefHandle() = default;

    // Take ownership of a raw new-allocated pointer
    static RefHandle adopt(T* p) {
        if (!p) return {};
        RefHandle h;
        h.blk_ = rb_alloc(p, [](void* o){ delete static_cast<T*>(o); });
        h.ptr_ = p;
        return h;
    }

    // Share an existing control block (used by ref_static_cast)
    RefHandle(RefBlock* blk, T* ptr) : blk_(blk), ptr_(ptr) { if (blk_) rb_retain(blk_); }

    RefHandle(const RefHandle& o) : ptr_(o.ptr_), blk_(o.blk_) { if (blk_) rb_retain(blk_); }
    RefHandle& operator=(const RefHandle& o) {
        if (this != &o) {
            _release(); ptr_ = o.ptr_; blk_ = o.blk_;
            if (blk_) rb_retain(blk_);
        }
        return *this;
    }
    RefHandle(RefHandle&& o) noexcept : ptr_(o.ptr_), blk_(o.blk_)
        { o.ptr_ = nullptr; o.blk_ = nullptr; }
    RefHandle& operator=(RefHandle&& o) noexcept {
        if (this != &o) {
            _release(); ptr_ = o.ptr_; blk_ = o.blk_;
            o.ptr_ = nullptr; o.blk_ = nullptr;
        }
        return *this;
    }
    ~RefHandle() { _release(); }

    T*       get()            const { return ptr_; }
    T*       operator->()     const { return ptr_; }
    T&       operator*()      const { return *ptr_; }
    explicit operator bool()  const { return ptr_ != nullptr; }
    int      use_count()      const { return blk_ ? blk_->refs : 0; }
    RefBlock* block()         const { return blk_; }
    void reset()                    { _release(); }

private:
    void _release() {
        if (blk_) rb_release(blk_);
        ptr_ = nullptr; blk_ = nullptr;
    }
    T*        ptr_ = nullptr;
    RefBlock* blk_ = nullptr;
};

// ── Type-erased strong handle ─────────────────────────────────────────────────
template<>
class RefHandle<void> {
public:
    RefHandle() = default;

    template<typename T>
    RefHandle(const RefHandle<T>& o) : blk_(o.block()) { if (blk_) rb_retain(blk_); }

    RefHandle(const RefHandle& o) : blk_(o.blk_) { if (blk_) rb_retain(blk_); }
    RefHandle& operator=(const RefHandle& o) {
        if (this != &o) { _release(); blk_ = o.blk_; if (blk_) rb_retain(blk_); }
        return *this;
    }
    template<typename T>
    RefHandle& operator=(const RefHandle<T>& o) {
        _release(); blk_ = o.block(); if (blk_) rb_retain(blk_); return *this;
    }
    RefHandle(RefHandle&& o) noexcept : blk_(o.blk_) { o.blk_ = nullptr; }
    RefHandle& operator=(RefHandle&& o) noexcept {
        if (this != &o) { _release(); blk_ = o.blk_; o.blk_ = nullptr; }
        return *this;
    }
    ~RefHandle() { _release(); }

    explicit operator bool() const { return blk_ && blk_->refs > 0; }
    int       use_count()    const { return blk_ ? blk_->refs : 0; }
    RefBlock* block()        const { return blk_; }
    void reset()                   { _release(); }

private:
    void _release() { if (blk_) rb_release(blk_); blk_ = nullptr; }
    RefBlock* blk_ = nullptr;
};

// ── ref_static_cast — type-erased → typed (caller ensures T matches) ──────────
template<typename To, typename From>
inline RefHandle<To> ref_static_cast(const RefHandle<From>& from) {
    if (!from.block()) return {};
    return RefHandle<To>(from.block(), static_cast<To*>(from.block()->ptr));
}

#endif
