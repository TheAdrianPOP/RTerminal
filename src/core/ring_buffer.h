#pragma once
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

// Fixed-capacity, single-producer/single-consumer ring buffer.
// Lock-free for SPSC use; use external mutex for MPSC/MPMC.

#include "math_utils.h"
#include "atomic.h"
#include "move.h"
#include <cassert>
#include <cstring>

template<typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "RingBuffer capacity must be a power of 2");
public:
    RingBuffer() : head_(0), tail_(0) {}

    // Push (producer side). Returns false if full.
    bool push(const T& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false; // full
        new (&buf_[h]) T(val);
        head_.store(next, rt_mo::release);
        return true;
    }
    bool push(T&& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        new (&buf_[h]) T(rt_move(val));
        head_.store(next, rt_mo::release);
        return true;
    }

    // Pop (consumer side). Returns false if empty.
    bool pop(T& out) {
        size_t t = tail_.load(rt_mo::relaxed);
        if (t == head_.load(rt_mo::acquire)) return false; // empty
        out = rt_move(buf_[t]);
        if constexpr (!__is_trivially_destructible(T)) buf_[t].~T();
        tail_.store((t + 1) & mask_, rt_mo::release);
        return true;
    }

    // Peek without removing. Returns nullptr if empty.
    const T* peek() const {
        size_t t = tail_.load(rt_mo::acquire);
        if (t == head_.load(rt_mo::acquire)) return nullptr;
        return &buf_[t];
    }

    bool   empty() const { return tail_.load(rt_mo::acquire) == head_.load(rt_mo::acquire); }
    bool   full()  const { return ((head_.load(rt_mo::relaxed) + 1) & mask_) == tail_.load(rt_mo::acquire); }
    size_t size()  const {
        size_t h = head_.load(rt_mo::acquire);
        size_t t = tail_.load(rt_mo::acquire);
        return (h - t) & mask_;
    }
    static constexpr size_t capacity() { return N; }
    void clear() { T tmp; while (pop(tmp)); }

private:
    static constexpr size_t mask_ = N - 1;
    alignas(T) T buf_[N];
    AtomicSize head_;
    AtomicSize tail_;
};

// Heap-allocated variant for runtime-determined capacity
template<typename T>
class DynRingBuffer {
public:
    explicit DynRingBuffer(size_t cap)
        : mask_(roundCap(cap) - 1), buf_(new T[mask_ + 1]), head_(0), tail_(0) {}
    ~DynRingBuffer() { delete[] buf_; }

    bool push(const T& val) {
        size_t h = head_.load(rt_mo::relaxed);
        size_t next = (h + 1) & mask_;
        if (next == tail_.load(rt_mo::acquire)) return false;
        buf_[h] = val;
        head_.store(next, rt_mo::release);
        return true;
    }
    bool pop(T& out) {
        size_t t = tail_.load(rt_mo::relaxed);
        if (t == head_.load(rt_mo::acquire)) return false;
        out = buf_[t];
        tail_.store((t + 1) & mask_, rt_mo::release);
        return true;
    }
    bool   empty()    const { return tail_.load(rt_mo::acquire) == head_.load(rt_mo::acquire); }
    size_t capacity() const { return mask_ + 1; }

private:
    static size_t roundCap(size_t n) { size_t p=1; while(p<n) p<<=1; return p; }
    size_t     mask_;
    T*         buf_;
    AtomicSize head_, tail_;
};

#endif
