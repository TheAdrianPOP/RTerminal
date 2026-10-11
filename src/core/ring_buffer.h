#pragma once
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

// Fixed-capacity, single-producer/single-consumer ring buffer.
// Lock-free for SPSC use; use external mutex for MPSC/MPMC.

#include "math_utils.h"
#include "atomic.h"
#include "move.h"
#include "internal.h"

template<typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "RingBuffer capacity must be a power of 2");
    static_assert(N > 0, "RingBuffer capacity must be greater than 0");

public:
    RingBuffer() : head_(0), tail_(0) {}

    ~RingBuffer() {
        clear();
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    // Push (producer side). Returns false if full.
    bool push(const T& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false; // full
        new (elem(h)) T(val);
        head_.store(next, rt_mo::release);
        return true;
    }

    bool push(T&& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        new (elem(h)) T(rt_move(val));
        head_.store(next, rt_mo::release);
        return true;
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        new (elem(h)) T(rt_forward<Args>(args)...);
        head_.store(next, rt_mo::release);
        return true;
    }

    // Pop (consumer side). Returns false if empty.
    bool pop(T& out) {
        size_t t = tail_.load(rt_mo::relaxed);
        if (t == head_.load(rt_mo::acquire)) return false; // empty
        T* src = elem(t);
        out = rt_move(*src);
        if constexpr (!__is_trivially_destructible(T)) {
            src->~T();
        }
        tail_.store((t + 1) & mask_, rt_mo::release);
        return true;
    }

    // Peek without removing. Returns nullptr if empty.
    const T* peek() const {
        size_t t = tail_.load(rt_mo::acquire);
        if (t == head_.load(rt_mo::acquire)) return nullptr;
        return elem(t);
    }

    T* peek() {
        size_t t = tail_.load(rt_mo::acquire);
        if (t == head_.load(rt_mo::acquire)) return nullptr;
        return elem(t);
    }

    bool empty() const {
        return tail_.load(rt_mo::acquire) == head_.load(rt_mo::acquire);
    }

    bool full() const {
        return ((head_.load(rt_mo::relaxed) + 1) & mask_) == tail_.load(rt_mo::acquire);
    }

    size_t size() const {
        size_t h = head_.load(rt_mo::acquire);
        size_t t = tail_.load(rt_mo::acquire);
        return (h - t) & mask_;
    }

    static constexpr size_t capacity() { return N; }

    void clear() {
        size_t t = tail_.load(rt_mo::relaxed);
        size_t h = head_.load(rt_mo::relaxed);
        while (t != h) {
            if constexpr (!__is_trivially_destructible(T)) {
                elem(t)->~T();
            }
            t = (t + 1) & mask_;
        }
        tail_.store(t, rt_mo::release);
    }

private:
    static constexpr size_t mask_ = N - 1;
    alignas(alignof(T)) uint8_t storage_[N * sizeof(T)];
    AtomicSize head_;
    AtomicSize tail_;

    FORCE_INLINE T* elem(size_t index) {
        return reinterpret_cast<T*>(&storage_[index * sizeof(T)]);
    }

    FORCE_INLINE const T* elem(size_t index) const {
        return reinterpret_cast<const T*>(&storage_[index * sizeof(T)]);
    }
};

// Heap-allocated variant for runtime-determined capacity
template<typename T>
class DynRingBuffer {
public:
    explicit DynRingBuffer(size_t cap)
        : mask_(roundCap(cap) - 1)
        , storage_(static_cast<uint8_t*>(malloc((mask_ + 1) * sizeof(T))))
        , head_(0)
        , tail_(0)
    {}

    ~DynRingBuffer() {
        clear();
        free(storage_);
    }

    DynRingBuffer(const DynRingBuffer&) = delete;
    DynRingBuffer& operator=(const DynRingBuffer&) = delete;
    DynRingBuffer(DynRingBuffer&&) = delete;
    DynRingBuffer& operator=(DynRingBuffer&&) = delete;

    bool push(const T& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        new (elem(h)) T(val);
        head_.store(next, rt_mo::release);
        return true;
    }

    bool push(T&& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        new (elem(h)) T(rt_move(val));
        head_.store(next, rt_mo::release);
        return true;
    }

    template<typename... Args>
    bool emplace(Args&&... args) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        new (elem(h)) T(rt_forward<Args>(args)...);
        head_.store(next, rt_mo::release);
        return true;
    }

    bool pop(T& out) {
        size_t t = tail_.load(rt_mo::relaxed);
        if (t == head_.load(rt_mo::acquire)) return false;
        T* src = elem(t);
        out = rt_move(*src);
        if constexpr (!__is_trivially_destructible(T)) {
            src->~T();
        }
        tail_.store((t + 1) & mask_, rt_mo::release);
        return true;
    }

    bool empty() const {
        return tail_.load(rt_mo::acquire) == head_.load(rt_mo::acquire);
    }

    bool full() const {
        return ((head_.load(rt_mo::relaxed) + 1) & mask_) == tail_.load(rt_mo::acquire);
    }

    size_t capacity() const { return mask_ + 1; }

    void clear() {
        size_t t = tail_.load(rt_mo::relaxed);
        size_t h = head_.load(rt_mo::relaxed);
        while (t != h) {
            if constexpr (!__is_trivially_destructible(T)) {
                elem(t)->~T();
            }
            t = (t + 1) & mask_;
        }
        tail_.store(t, rt_mo::release);
    }

private:
    static size_t roundCap(size_t n) {
        if (n < 2) n = 2;
        size_t p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    size_t     mask_;
    uint8_t*   storage_;
    AtomicSize head_;
    AtomicSize tail_;

    FORCE_INLINE T* elem(size_t index) {
        return reinterpret_cast<T*>(&storage_[index * sizeof(T)]);
    }

    FORCE_INLINE const T* elem(size_t index) const {
        return reinterpret_cast<const T*>(&storage_[index * sizeof(T)]);
    }
};

#endif