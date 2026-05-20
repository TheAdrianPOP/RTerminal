#pragma once

#include "math_utils.h"
#include <cstring>

// Platform atomics — no <atomic> required.
// Uses _Interlocked* on MSVC, __atomic_* builtins on GCC/Clang.
// Targets x86/x64 (TSO): hardware loads are acquire, stores are release.
// We only need compiler barriers to prevent reordering at the IR level.

#ifdef _MSC_VER
#  include <intrin.h>
#  pragma intrinsic(_InterlockedExchange, _InterlockedExchangeAdd, _InterlockedCompareExchange)
#  ifdef _M_X64
#    pragma intrinsic(_InterlockedExchangeAdd64, _InterlockedCompareExchange64)
#  endif
#endif

// Memory-order constants.  Values intentionally match GCC's __ATOMIC_* macros.
namespace rt_mo {
    constexpr int relaxed = 0;
    constexpr int consume = 1;
    constexpr int acquire = 2;
    constexpr int release = 3;
    constexpr int acq_rel = 4;
    constexpr int seq_cst = 5;
    using Order = int;
}

// ── Platform primitives ───────────────────────────────────────────────────────
namespace rt_atomic_impl {

#ifdef _MSC_VER

FORCE_INLINE void compiler_barrier() { _ReadWriteBarrier(); }

FORCE_INLINE int load_i32(const volatile int* p, int order) {
    int v = *p;
    if (order >= rt_mo::acquire) compiler_barrier();
    return v;
}
FORCE_INLINE void store_i32(volatile int* p, int v, int order) {
    if (order >= rt_mo::release) compiler_barrier();
    *p = v;
}
FORCE_INLINE int fetch_add_i32(volatile int* p, int n) {
    return (int)_InterlockedExchangeAdd((volatile long*)p, (long)n);
}
FORCE_INLINE bool cmpxchg_i32(volatile int* p, int* expected, int desired) {
    long prev = _InterlockedCompareExchange((volatile long*)p, (long)desired, (long)*expected);
    if (prev == (long)*expected) return true;
    *expected = (int)prev; return false;
}

// bool stored as int
FORCE_INLINE bool load_bool(const volatile int* p, int order) { return load_i32(p, order) != 0; }
FORCE_INLINE void store_bool(volatile int* p, bool v, int order) { store_i32(p, v ? 1 : 0, order); }

// float stored as int bits
FORCE_INLINE float load_f32(const volatile int* p, int order) {
    int bits = load_i32(p, order); float f; memcpy(&f, &bits, 4); return f;
}
FORCE_INLINE void store_f32(volatile int* p, float v, int order) {
    int bits; memcpy(&bits, &v, 4); store_i32(p, bits, order);
}
FORCE_INLINE bool cmpxchg_f32(volatile int* p, float* expected, float desired) {
    int exp_bits; memcpy(&exp_bits, expected, 4);
    int des_bits; memcpy(&des_bits, &desired, 4);
    long prev = _InterlockedCompareExchange((volatile long*)p, (long)des_bits, (long)exp_bits);
    if (prev == (long)exp_bits) return true;
    memcpy(expected, &prev, 4); return false;
}

// size_t
#  if defined(_M_X64)
FORCE_INLINE size_t load_sz(const volatile size_t* p, int order) {
    size_t v = *p; if (order >= rt_mo::acquire) compiler_barrier(); return v;
}
FORCE_INLINE void store_sz(volatile size_t* p, size_t v, int order) {
    if (order >= rt_mo::release) compiler_barrier(); *p = v;
}
FORCE_INLINE size_t fetch_add_sz(volatile size_t* p, size_t n) {
    return (size_t)_InterlockedExchangeAdd64((volatile __int64*)p, (__int64)n);
}
#  else
FORCE_INLINE size_t load_sz(const volatile size_t* p, int order) { return (size_t)load_i32((const volatile int*)p, order); }
FORCE_INLINE void   store_sz(volatile size_t* p, size_t v, int order) { store_i32((volatile int*)p, (int)v, order); }
FORCE_INLINE size_t fetch_add_sz(volatile size_t* p, size_t n) { return (size_t)fetch_add_i32((volatile int*)p, (int)n); }
#  endif

#else // GCC / Clang — __atomic_* builtins, no header needed

FORCE_INLINE int  load_i32(const volatile int* p, int order)            { return __atomic_load_n(p, order); }
FORCE_INLINE void store_i32(volatile int* p, int v, int order)          { __atomic_store_n(p, v, order); }
FORCE_INLINE int  fetch_add_i32(volatile int* p, int n)                 { return __atomic_fetch_add(p, n, __ATOMIC_ACQ_REL); }
FORCE_INLINE bool cmpxchg_i32(volatile int* p, int* exp, int desired)  { return __atomic_compare_exchange_n(p, exp, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
FORCE_INLINE bool load_bool(const volatile int* p, int order)           { return load_i32(p, order) != 0; }
FORCE_INLINE void store_bool(volatile int* p, bool v, int order)        { store_i32(p, v ? 1 : 0, order); }
FORCE_INLINE float load_f32(const volatile int* p, int order)           { int b = load_i32(p, order); float f; memcpy(&f, &b, 4); return f; }
FORCE_INLINE void  store_f32(volatile int* p, float v, int order)       { int b; memcpy(&b, &v, 4); store_i32(p, b, order); }
FORCE_INLINE bool  cmpxchg_f32(volatile int* p, float* exp, float des) {
    int eb; memcpy(&eb, exp, 4); int db; memcpy(&db, &des, 4);
    bool ok = __atomic_compare_exchange_n(p, &eb, db, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (!ok) memcpy(exp, &eb, 4); return ok;
}
FORCE_INLINE size_t load_sz(const volatile size_t* p, int order)       { return __atomic_load_n(p, order); }
FORCE_INLINE void   store_sz(volatile size_t* p, size_t v, int order)  { __atomic_store_n(p, v, order); }
FORCE_INLINE size_t fetch_add_sz(volatile size_t* p, size_t n)         { return __atomic_fetch_add(p, n, __ATOMIC_ACQ_REL); }

#endif
} // namespace rt_atomic_impl

// ── Public wrappers ───────────────────────────────────────────────────────────

struct alignas(4) AtomicInt {
    AtomicInt() : val_(0) {}
    explicit AtomicInt(int v) : val_(v) {}
    AtomicInt(const AtomicInt&) = delete;
    AtomicInt& operator=(const AtomicInt&) = delete;

    FORCE_INLINE void store(int v, rt_mo::Order o = rt_mo::release)     { rt_atomic_impl::store_i32(&val_, v, o); }
    FORCE_INLINE int  load(rt_mo::Order o = rt_mo::acquire)       const  { return rt_atomic_impl::load_i32(&val_, o); }
    FORCE_INLINE int  fetch_add(int n, rt_mo::Order = rt_mo::acq_rel)   { return rt_atomic_impl::fetch_add_i32(&val_, n); }
    FORCE_INLINE int  fetch_sub(int n, rt_mo::Order = rt_mo::acq_rel)   { return rt_atomic_impl::fetch_add_i32(&val_, -n); }
    FORCE_INLINE bool compare_exchange(int& exp, int desired)            { return rt_atomic_impl::cmpxchg_i32(&val_, &exp, desired); }
private:
    mutable volatile int val_;
};

struct alignas(4) AtomicBool {
    AtomicBool() : val_(0) {}
    explicit AtomicBool(bool v) : val_(v ? 1 : 0) {}
    AtomicBool(const AtomicBool&) = delete;
    AtomicBool& operator=(const AtomicBool&) = delete;

    FORCE_INLINE void store(bool v, rt_mo::Order o = rt_mo::release)    { rt_atomic_impl::store_bool(&val_, v, o); }
    FORCE_INLINE bool load(rt_mo::Order o = rt_mo::acquire)       const  { return rt_atomic_impl::load_bool(&val_, o); }
private:
    mutable volatile int val_;
};

struct alignas(4) AtomicFloat {
    AtomicFloat() : val_(0) {}
    explicit AtomicFloat(float v) { int b; memcpy(&b, &v, 4); val_ = b; }
    AtomicFloat(const AtomicFloat&) = delete;
    AtomicFloat& operator=(const AtomicFloat&) = delete;

    FORCE_INLINE void  store(float v, rt_mo::Order o = rt_mo::release)  { rt_atomic_impl::store_f32(&val_, v, o); }
    FORCE_INLINE float load(rt_mo::Order o = rt_mo::acquire)      const  { return rt_atomic_impl::load_f32(&val_, o); }
    FORCE_INLINE float fetch_add(float delta, rt_mo::Order = rt_mo::acq_rel) {
        float cur = load(rt_mo::relaxed);
        while (!rt_atomic_impl::cmpxchg_f32(&val_, &cur, cur + delta));
        return cur;
    }
private:
    mutable volatile int val_;
};

// size_t atomic — used by RingBuffer
struct alignas(8) AtomicSize {
    AtomicSize() : val_(0) {}
    explicit AtomicSize(size_t v) : val_(v) {}
    AtomicSize(const AtomicSize&) = delete;
    AtomicSize& operator=(const AtomicSize&) = delete;

    FORCE_INLINE void   store(size_t v, rt_mo::Order o = rt_mo::release) { rt_atomic_impl::store_sz(&val_, v, o); }
    FORCE_INLINE size_t load(rt_mo::Order o = rt_mo::acquire)      const  { return rt_atomic_impl::load_sz(&val_, o); }
    FORCE_INLINE size_t fetch_add(size_t n, rt_mo::Order = rt_mo::acq_rel) { return rt_atomic_impl::fetch_add_sz(&val_, n); }
private:
    mutable volatile size_t val_;
};