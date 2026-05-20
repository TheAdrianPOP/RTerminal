#ifndef VECTOR_H
#define VECTOR_H

#include "math_utils.h"
#include <immintrin.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include "arena.h"
#include "../core/move.h"

// <new> is C++ language infrastructure (placement new only) — not an STL container.
// It is always safe to include and resolves placement new for PodVector.
#include <new>

// ── Portable type-trait builtins ──────────────────────────────────────────────
// GCC uses __has_trivial_* while MSVC/Clang expose __is_trivially_*.
#if defined(__GNUC__) && !defined(__clang__)
#  define RT_IS_TRIVIALLY_COPYABLE(T)     __is_trivially_copyable(T)
#  define RT_IS_TRIVIALLY_DESTRUCTIBLE(T) __has_trivial_destructor(T)
#  define RT_IS_TRIVIALLY_CONSTRUCTIBLE(T)__has_trivial_constructor(T)
#else
#  define RT_IS_TRIVIALLY_COPYABLE(T)     __is_trivially_copyable(T)
#  define RT_IS_TRIVIALLY_DESTRUCTIBLE(T) __is_trivially_destructible(T)
#  define RT_IS_TRIVIALLY_CONSTRUCTIBLE(T)__is_trivially_constructible(T)
#endif

struct alignas(16) Vec3 {
    float x, y, z, w;

    FORCE_INLINE Vec3() { store(_mm_setzero_ps()); }
    FORCE_INLINE Vec3(float _x, float _y, float _z) { store(_mm_set_ps(0.0f, _z, _y, _x)); }
    FORCE_INLINE Vec3(float _f) { store(_mm_set1_ps(_f)); }
    FORCE_INLINE Vec3(__m128 m) { store(m); }

    FORCE_INLINE __m128 load() const { return _mm_load_ps(&x); }
    FORCE_INLINE void store(__m128 m) { _mm_store_ps(&x, m); }

    FORCE_INLINE Vec3 operator-() const {
        return Vec3(_mm_xor_ps(load(), _mm_set1_ps(-0.0f)));
    }

    FORCE_INLINE Vec3 operator+(const Vec3& b) const { return Vec3(_mm_add_ps(load(), b.load())); }
    FORCE_INLINE Vec3 operator-(const Vec3& b) const { return Vec3(_mm_sub_ps(load(), b.load())); }
    FORCE_INLINE Vec3 operator*(const Vec3& b) const { return Vec3(_mm_mul_ps(load(), b.load())); }
    FORCE_INLINE Vec3 operator*(float s) const { return Vec3(_mm_mul_ps(load(), _mm_set1_ps(s))); }
    FORCE_INLINE Vec3 operator/(float s) const { return Vec3(_mm_mul_ps(load(), _mm_set1_ps(1.0f / s))); }

    FORCE_INLINE Vec3& operator+=(const Vec3& b) { store(_mm_add_ps(load(), b.load())); return *this; }
    FORCE_INLINE Vec3& operator-=(const Vec3& b) { store(_mm_sub_ps(load(), b.load())); return *this; }
    FORCE_INLINE Vec3& operator*=(float s) { store(_mm_mul_ps(load(), _mm_set1_ps(s))); return *this; }

    FORCE_INLINE Vec3 min(const Vec3& b) const { return Vec3(_mm_min_ps(load(), b.load())); }
    FORCE_INLINE Vec3 max(const Vec3& b) const { return Vec3(_mm_max_ps(load(), b.load())); }

    FORCE_INLINE float dot(const Vec3& b) const {
#ifdef __SSE4_1__
        return _mm_cvtss_f32(_mm_dp_ps(load(), b.load(), 0x71));
#else
        __m128 ml = load();
        __m128 m = _mm_mul_ps(ml, b.load());
        __m128 y = _mm_shuffle_ps(m, m, _MM_SHUFFLE(1, 1, 1, 1));
        __m128 z = _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 2, 2, 2));
        return _mm_cvtss_f32(_mm_add_ss(_mm_add_ss(m, y), z));
#endif
    }

    FORCE_INLINE Vec3 cross(const Vec3& b) const {
        return Vec3(_mm_sub_ps(
            _mm_mul_ps(_mm_shuffle_ps(load(), load(), _MM_SHUFFLE(3, 0, 2, 1)), _mm_shuffle_ps(b.load(), b.load(), _MM_SHUFFLE(3, 1, 0, 2))),
            _mm_mul_ps(_mm_shuffle_ps(load(), load(), _MM_SHUFFLE(3, 1, 0, 2)), _mm_shuffle_ps(b.load(), b.load(), _MM_SHUFFLE(3, 0, 2, 1)))
        ));
    }

    FORCE_INLINE float magnitudeSq() const { return dot(*this); }
    FORCE_INLINE float magnitude() const { return fast_sqrt(dot(*this)); }

    FORCE_INLINE Vec3 normalize() const {
        __m128 ml  = load();
        __m128 m2  = _mm_mul_ps(ml, ml);
        __m128 sum = _mm_add_ss(m2, _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(1, 1, 1, 1)));
        sum        = _mm_add_ss(sum, _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(2, 2, 2, 2)));

        float lenSq;
        _mm_store_ss(&lenSq, sum);
        if (is_zero_lenSq(lenSq)) return Vec3(0.0f, 0.0f, 0.0f);

        __m128 r  = _mm_rsqrt_ss(sum);
        __m128 nr = _mm_mul_ss(_mm_mul_ss(sum, r), r);
        r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(1.5f),
                                      _mm_mul_ss(_mm_set_ss(0.5f), nr)));
        r = _mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 0, 0, 0));
        return Vec3(_mm_mul_ps(ml, r));
    }

    FORCE_INLINE float hmax3() const { return fast_max(x, fast_max(y, z)); }
    FORCE_INLINE float hmin3() const { return fast_min(x, fast_min(y, z)); }
};

struct Vec2 {
    // Dual naming: (x,y) for position, (u,v) for texture coords
    union { float x, u; };
    union { float y, v; };

    Vec2() : x(0), y(0) {}
    Vec2(float _x, float _y) : x(_x), y(_y) {}
    explicit Vec2(float f) : x(f), y(f) {}

    FORCE_INLINE Vec2 operator-()              const { return {-x, -y}; }
    FORCE_INLINE Vec2 operator+(Vec2 b)        const { return {x+b.x, y+b.y}; }
    FORCE_INLINE Vec2 operator-(Vec2 b)        const { return {x-b.x, y-b.y}; }
    FORCE_INLINE Vec2 operator*(Vec2 b)        const { return {x*b.x, y*b.y}; }
    FORCE_INLINE Vec2 operator*(float s)       const { return {x*s, y*s}; }
    FORCE_INLINE Vec2 operator/(float s)       const { float inv=1.f/s; return {x*inv, y*inv}; }
    FORCE_INLINE Vec2& operator+=(Vec2 b)            { x+=b.x; y+=b.y; return *this; }
    FORCE_INLINE Vec2& operator-=(Vec2 b)            { x-=b.x; y-=b.y; return *this; }
    FORCE_INLINE Vec2& operator*=(float s)           { x*=s;   y*=s;   return *this; }
    FORCE_INLINE bool  operator==(Vec2 b)      const { return x==b.x && y==b.y; }
    FORCE_INLINE bool  operator!=(Vec2 b)      const { return x!=b.x || y!=b.y; }

    FORCE_INLINE float dot(Vec2 b)             const { return x*b.x + y*b.y; }
    FORCE_INLINE float cross(Vec2 b)           const { return x*b.y - y*b.x; }  // scalar cross (z-component)
    FORCE_INLINE float lengthSq()              const { return x*x + y*y; }
    FORCE_INLINE float length()                const { return fast_sqrt(x*x + y*y); }
    FORCE_INLINE Vec2  normalize()             const { float l=length(); return is_zero_f(l)?Vec2(0,0):(*this/l); }
    FORCE_INLINE Vec2  perp()                  const { return {-y, x}; }   // 90° CCW
    FORCE_INLINE Vec2  lerp(Vec2 b, float t)   const { return {fast_lerp(x,b.x,t), fast_lerp(y,b.y,t)}; }
    FORCE_INLINE Vec2  min(Vec2 b)             const { return {fast_min(x,b.x), fast_min(y,b.y)}; }
    FORCE_INLINE Vec2  max(Vec2 b)             const { return {fast_max(x,b.x), fast_max(y,b.y)}; }
    FORCE_INLINE Vec2  abs()                   const { return {fast_abs(x), fast_abs(y)}; }
    FORCE_INLINE float dist(Vec2 b)            const { return (*this-b).length(); }
};
FORCE_INLINE Vec2 operator*(float s, Vec2 v) { return v*s; }

// Integer 2D vector — tile coords, screen pixels, grid positions
struct Vec2i {
    int x, y;
    Vec2i() : x(0), y(0) {}
    Vec2i(int _x, int _y) : x(_x), y(_y) {}
    explicit Vec2i(Vec2 v) : x((int)v.x), y((int)v.y) {}

    FORCE_INLINE Vec2i operator+(Vec2i b)  const { return {x+b.x, y+b.y}; }
    FORCE_INLINE Vec2i operator-(Vec2i b)  const { return {x-b.x, y-b.y}; }
    FORCE_INLINE Vec2i operator*(int s)    const { return {x*s,   y*s};   }
    FORCE_INLINE Vec2i& operator+=(Vec2i b)      { x+=b.x; y+=b.y; return *this; }
    FORCE_INLINE Vec2i& operator-=(Vec2i b)      { x-=b.x; y-=b.y; return *this; }
    FORCE_INLINE bool   operator==(Vec2i b) const { return x==b.x && y==b.y; }
    FORCE_INLINE bool   operator!=(Vec2i b) const { return x!=b.x || y!=b.y; }
    FORCE_INLINE Vec2   toFloat()           const { return {(float)x, (float)y}; }
};

constexpr size_t kPodVecMinCap   = 4;
constexpr size_t kPodVecMinAlign = 16;

// Minimal reverse iterator — avoids <iterator>.
template<typename Iter>
struct PodVecRevIter {
    Iter cur;
    explicit PodVecRevIter(Iter i) : cur(i) {}
    Iter  base()                           const { return cur; }
    auto& operator*()                      const { Iter t = cur; --t; return *t; }
    auto  operator->()                     const { Iter t = cur; --t; return &(*t); }
    PodVecRevIter& operator++()                  { --cur; return *this; }
    PodVecRevIter  operator++(int)               { PodVecRevIter t = *this; --cur; return t; }
    PodVecRevIter& operator--()                  { ++cur; return *this; }
    bool operator==(const PodVecRevIter& r) const { return cur == r.cur; }
    bool operator!=(const PodVecRevIter& r) const { return cur != r.cur; }
};

template <typename T>
class PodVector {
public:
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = PodVecRevIter<iterator>;
    using const_reverse_iterator = PodVecRevIter<const_iterator>;

    PodVector() : m_data(nullptr), m_size(0), m_capacity(0), m_arena(nullptr) {}
    explicit PodVector(LinearArena* arena) : m_data(nullptr), m_size(0), m_capacity(0), m_arena(arena) {}
    explicit PodVector(size_t n, LinearArena* arena = nullptr) : m_data(nullptr), m_size(0), m_capacity(0), m_arena(arena) { resize(n); }

    ~PodVector() { clear(); if (m_data && !m_arena) aligned_free(m_data); }

    PodVector(const PodVector& other) : m_data(nullptr), m_size(0), m_capacity(0), m_arena(other.m_arena) {
        if (other.m_size > 0) {
            reserve(other.m_size);
            if constexpr (RT_IS_TRIVIALLY_COPYABLE(T)) {
                memcpy(m_data, other.m_data, other.m_size * sizeof(T));
            } else {
                for (size_t i = 0; i < other.m_size; ++i) new (&m_data[i]) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }
    }

    PodVector& operator=(const PodVector& other) {
        if (this != &other) {
            clear();
            reserve(other.m_size);
            if constexpr (RT_IS_TRIVIALLY_COPYABLE(T)) {
                memcpy(m_data, other.m_data, other.m_size * sizeof(T));
            } else {
                for (size_t i = 0; i < other.m_size; ++i) new (&m_data[i]) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }
        return *this;
    }

    PodVector(PodVector&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size),
          m_capacity(other.m_capacity), m_arena(other.m_arena) {
        other.m_data = nullptr; other.m_size = 0; other.m_capacity = 0;
    }

    PodVector& operator=(PodVector&& other) noexcept {
        if (this != &other) {
            if (m_data && !m_arena) aligned_free(m_data);
            m_data     = other.m_data;     m_size     = other.m_size;
            m_capacity = other.m_capacity; m_arena    = other.m_arena;
            other.m_data = nullptr; other.m_size = 0; other.m_capacity = 0;
        }
        return *this;
    }

    FORCE_INLINE T& operator[](size_t i) { assert(i < m_size); return m_data[i]; }
    FORCE_INLINE const T& operator[](size_t i) const { assert(i < m_size); return m_data[i]; }

    T& back()              { assert(m_size > 0); return m_data[m_size - 1]; }
    const T& back()  const { assert(m_size > 0); return m_data[m_size - 1]; }
    T& front()             { assert(m_size > 0); return m_data[0]; }
    const T& front() const { assert(m_size > 0); return m_data[0]; }

    T*       data()       { return m_data; }
    const T* data() const { return m_data; }
    size_t   size()       const { return m_size; }
    size_t   capacity()   const { return m_capacity; }
    bool     empty()      const { return m_size == 0; }

    void clear() {
        if constexpr (!RT_IS_TRIVIALLY_DESTRUCTIBLE(T)) {
            for (size_t i = 0; i < m_size; ++i) m_data[i].~T();
        }
        m_size = 0;
    }

    void assign(size_t n, const T& val) {
        clear();
        reserve(n);
        for (size_t i = 0; i < n; ++i) new (&m_data[i]) T(val);
        m_size = n;
    }

    iterator erase(iterator it) {
        const size_t index = static_cast<size_t>(it - m_data);
        assert(index < m_size);

        if constexpr (RT_IS_TRIVIALLY_COPYABLE(T)) {
            const size_t move_count = m_size - index - 1;
            if (move_count > 0)
                memmove(&m_data[index], &m_data[index + 1], move_count * sizeof(T));
        } else {
            for (size_t i = index; i < m_size - 1; ++i) {
                m_data[i] = rt_move(m_data[i + 1]);
            }
            m_data[m_size - 1].~T();
        }

        --m_size;
        return it;
    }

    void shrink_to_fit() {
        if (m_size == m_capacity) return;
        if (m_size == 0) {
            if (m_data && !m_arena) aligned_free(m_data);
            m_data = nullptr; m_capacity = 0;
            return;
        }
        reallocate(m_size);
    }

    void reserve(size_t new_cap) {
        if (new_cap <= m_capacity) return;
        reallocate(new_cap);
    }

    void resize(size_t n) {
        if (n < m_size) {
            if constexpr (!RT_IS_TRIVIALLY_DESTRUCTIBLE(T)) {
                for (size_t i = n; i < m_size; ++i) m_data[i].~T();
            }
        } else if (n > m_size) {
            reserve(n);
            size_t old_size = m_size;
            for (size_t i = old_size; i < n; ++i) {
                new (&m_data[i]) T();
                m_size++;
            }
        }
        m_size = n;
    }

    void resize(size_t n, const T& val) {
        if (n < m_size) {
            if constexpr (!RT_IS_TRIVIALLY_DESTRUCTIBLE(T)) {
                for (size_t i = n; i < m_size; ++i) m_data[i].~T();
            }
        } else if (n > m_size) {
            reserve(n);
            size_t old_size = m_size;
            for (size_t i = old_size; i < n; ++i) {
                new (&m_data[i]) T(val);
                m_size++;
            }
        }
        m_size = n;
    }

    void resize_uninitialized(size_t n) {
        static_assert(RT_IS_TRIVIALLY_CONSTRUCTIBLE(T) &&
                      RT_IS_TRIVIALLY_DESTRUCTIBLE(T),
            "resize_uninitialized requires a trivially constructible and "
            "trivially destructible type — use resize() for non-POD types.");
        if (n > m_capacity) reserve(n);
        m_size = n;
    }

    void push_back(const T& val) {
        if (m_size == m_capacity) grow();
        new (&m_data[m_size++]) T(val);
    }

    void push_back(T&& val) {
        if (m_size == m_capacity) grow();
        new (&m_data[m_size++]) T(rt_move(val));
    }

    template<typename... Args>
    T& emplace_back(Args&&... args) {
        if (m_size == m_capacity) grow();
        T* ptr = new (&m_data[m_size++]) T(rt_forward<Args>(args)...);
        return *ptr;
    }

    void pop_back() {
        if (m_size > 0) {
            --m_size;
            if constexpr (!RT_IS_TRIVIALLY_DESTRUCTIBLE(T)) m_data[m_size].~T();
        }
    }

    iterator begin() { return m_data; }
    iterator end()   { return m_data + m_size; }
    const_iterator begin() const { return m_data; }
    const_iterator end()   const { return m_data + m_size; }
    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend()   { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend()   const { return const_reverse_iterator(begin()); }

private:
    void grow() {
        const size_t new_cap = m_capacity == 0
            ? kPodVecMinCap
            : m_capacity + (m_capacity >> 1);
        reserve(new_cap);
    }

    void reallocate(size_t new_cap) {
        const size_t alignment = alignof(T) > kPodVecMinAlign
            ? alignof(T) : kPodVecMinAlign;

        T* new_data = nullptr;
        if (m_arena) {
            new_data = static_cast<T*>(m_arena->alloc(new_cap * sizeof(T), alignment));
            assert(new_data && "LinearArena out of memory during PodVector reallocate!");
        } else {
            new_data = static_cast<T*>(aligned_alloc_internal(alignment, new_cap * sizeof(T)));
            if (!new_data) {
                assert(false && "Out of memory during PodVector reallocate!");
                abort();
            }
        }

        if (m_size > 0) {
            if constexpr (RT_IS_TRIVIALLY_COPYABLE(T)) {
                memcpy(new_data, m_data, m_size * sizeof(T));
            } else {
                for (size_t i = 0; i < m_size; ++i) {
                    new (&new_data[i]) T(rt_move(m_data[i]));
                    m_data[i].~T();
                }
            }
        }

        if (m_data && !m_arena) aligned_free(m_data);
        m_data     = new_data;
        m_capacity = new_cap;
    }

    static void* aligned_alloc_internal(size_t alignment, size_t size) {
#ifdef _WIN32
        return _aligned_malloc(size, alignment);
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
        return ptr;
#endif
    }

    static void aligned_free(void* ptr) {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }

    T*           m_data;
    size_t       m_size;
    size_t       m_capacity;
    LinearArena* m_arena;
};

#endif