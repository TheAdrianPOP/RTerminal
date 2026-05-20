#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <cmath>
#include <cstdint>
#include <cfloat>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define RTERMINAL_ARCH_X86
    #include <immintrin.h>
#else
    #error "This math library currently requires an x86/x64 architecture with at least SSE1 support."
#endif

#ifdef _MSC_VER
    #define FORCE_INLINE __forceinline
    #define RESTRICT __restrict
    #define ALIGN(x) __declspec(align(x))
#elif defined(__GNUC__) || defined(__clang__)
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define RESTRICT __restrict__
    #define ALIGN(x) __attribute__((aligned(x)))
#else
    #define FORCE_INLINE inline
    #define RESTRICT
    #define ALIGN(x)
#endif

constexpr float PI          = 3.14159265359f;
constexpr float INV_PI      = 0.31830988618f;
constexpr float HALF_PI     = 1.57079632679f;
constexpr float TWO_PI      = 6.28318530718f;
constexpr float MAX_FLOAT   = FLT_MAX;
constexpr float MIN_FLOAT   = -FLT_MAX;
constexpr float EPSILON_ABS = 1e-6f;
constexpr float EPSILON_SQ  = 1e-16f;

FORCE_INLINE uint32_t float_as_uint(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(uint32_t));
    return u;
}
FORCE_INLINE float uint_as_float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}

FORCE_INLINE float fast_sqrt(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}

FORCE_INLINE float fast_inv_sqrt(float number) {
    __m128 n = _mm_set_ss(number);
    __m128 r = _mm_rsqrt_ss(n);
    const __m128 three_halfs = _mm_set_ss(1.5f);
    const __m128 half        = _mm_set_ss(0.5f);
    __m128 muls = _mm_mul_ss(_mm_mul_ss(n, r), r);
    __m128 sub  = _mm_sub_ss(three_halfs, _mm_mul_ss(half, muls));
    return _mm_cvtss_f32(_mm_mul_ss(r, sub));
}

FORCE_INLINE float fast_min(float a, float b) { return (a < b) ? a : b; }
FORCE_INLINE float fast_max(float a, float b) { return (a > b) ? a : b; }
FORCE_INLINE int fast_floor(float x) { 
    int i = _mm_cvttss_si32(_mm_set_ss(x));
    return i - (i > x);
}

FORCE_INLINE float fast_round(float x) {
    return (float)_mm_cvttss_si32(_mm_set_ss(x >= 0.0f ? x + 0.5f : x - 0.5f));
}

template <typename T>
FORCE_INLINE T fast_clamp(T v, T lo, T hi) { return (v < lo) ? lo : ((v > hi) ? hi : v); }

template <typename T>
FORCE_INLINE T fast_min_t(T a, T b) { return a < b ? a : b; }

template <typename T>
FORCE_INLINE T fast_max_t(T a, T b) { return a > b ? a : b; }

FORCE_INLINE float fast_lerp(float a, float b, float t) { return a + t * (b - a); }
FORCE_INLINE float degToRad(float deg) { return deg * (PI / 180.0f); }
FORCE_INLINE float radToDeg(float rad) { return rad * (180.0f / PI); }
FORCE_INLINE float fast_abs(float a) { return std::abs(a); } 

FORCE_INLINE float epsilon_f(float a) {
    return fast_max(fast_abs(a) * FLT_EPSILON, EPSILON_ABS);
}
FORCE_INLINE float epsilon_ff(float a, float b) {
    return fast_max(fast_max(fast_abs(a), fast_abs(b)) * FLT_EPSILON, EPSILON_ABS);
}
FORCE_INLINE bool compare_ff(float a, float b, float max_diff) {
    return fast_abs(a - b) <= max_diff;
}
FORCE_INLINE bool compare_ff_relative(float a, float b) {
    return fast_abs(a - b) <= epsilon_ff(a, b);
}
FORCE_INLINE bool is_zero_f(float a) {
    return fast_abs(a) <= EPSILON_ABS;
}
FORCE_INLINE bool is_zero_lenSq(float lenSq) {
    return lenSq <= EPSILON_SQ;
}
FORCE_INLINE float safe_div(float a, float b) {
    return is_zero_f(b) ? 0.0f : (a / b);
}
static constexpr float kFastExpBias   = 126.94269504f;
static constexpr float kFastLogLinear = 1.1920928955078125e-7f;
static constexpr float kFloatMantissa = 8388608.0f;

FORCE_INLINE float fast_log2(float x) {
    float y = (float)float_as_uint(x);
    return y * kFastLogLinear - kFastExpBias;
}

FORCE_INLINE float fast_exp2(float x) {
    float y = x + kFastExpBias;
    uint32_t vy = (uint32_t)(int32_t)(y * kFloatMantissa);
    return uint_as_float(vy);
}

FORCE_INLINE float fast_pow(float b, float e) {
    return fast_exp2(e * fast_log2(b));
}

FORCE_INLINE float fast_sin(float x) {
    int32_t k_int = _mm_cvtss_si32(_mm_set_ss(x * INV_PI));
    float k = (float)k_int;
    const float P1 = 3.140625f;
    const float P2 = 0.000967653589793f;
    float r = (x - k * P1) - k * P2;
    float r2 = r * r;
    float res = -0.00018542f;             
    res = res * r2 + 0.00831348f;         
    res = res * r2 - 0.16665837f;         
    res = (res * r2 * r) + r;             
    uint32_t sign_flip = (uint32_t)(k_int & 1) << 31;
    uint32_t u = float_as_uint(res);
    u ^= sign_flip;
    return uint_as_float(u);
}

FORCE_INLINE float fast_cos(float x) { return fast_sin(x + HALF_PI); }
FORCE_INLINE float fast_tan(float x) { return fast_sin(x) / fast_cos(x); }
FORCE_INLINE float fast_asin(float x) { return std::asin(x); }
FORCE_INLINE float fast_atan2(float y, float x) { return std::atan2(y, x); }

struct ALIGN(16) SimdFloat4 {
    __m128 m;

    FORCE_INLINE SimdFloat4() : m(_mm_setzero_ps()) {}
    FORCE_INLINE SimdFloat4(__m128 v) : m(v) {}
    FORCE_INLINE SimdFloat4(float f) : m(_mm_set1_ps(f)) {}
    FORCE_INLINE SimdFloat4(const float* ptr) : m(_mm_loadu_ps(ptr)) {}
    FORCE_INLINE SimdFloat4(float a, float b, float c, float d) : m(_mm_set_ps(d, c, b, a)) {}

    FORCE_INLINE SimdFloat4 operator+(const SimdFloat4& r) const { return _mm_add_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator-(const SimdFloat4& r) const { return _mm_sub_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator*(const SimdFloat4& r) const { return _mm_mul_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator/(const SimdFloat4& r) const { return _mm_div_ps(m, r.m); }
    
    FORCE_INLINE SimdFloat4 operator&(const SimdFloat4& r) const { return _mm_and_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator|(const SimdFloat4& r) const { return _mm_or_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator^(const SimdFloat4& r) const { return _mm_xor_ps(m, r.m); }

    FORCE_INLINE SimdFloat4 operator<(const SimdFloat4& r) const { return _mm_cmplt_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator>(const SimdFloat4& r) const { return _mm_cmpgt_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator<=(const SimdFloat4& r) const { return _mm_cmple_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator>=(const SimdFloat4& r) const { return _mm_cmpge_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator==(const SimdFloat4& r) const { return _mm_cmpeq_ps(m, r.m); }
    FORCE_INLINE SimdFloat4 operator!=(const SimdFloat4& r) const { return _mm_cmpneq_ps(m, r.m); }
};

FORCE_INLINE SimdFloat4 simd_min(const SimdFloat4& a, const SimdFloat4& b) { return _mm_min_ps(a.m, b.m); }
FORCE_INLINE SimdFloat4 simd_max(const SimdFloat4& a, const SimdFloat4& b) { return _mm_max_ps(a.m, b.m); }

FORCE_INLINE SimdFloat4 simd_blend(const SimdFloat4& mask, const SimdFloat4& a, const SimdFloat4& b) {
#if defined(__SSE4_1__)
    return _mm_blendv_ps(b.m, a.m, mask.m);
#else
    return _mm_or_ps(_mm_and_ps(mask.m, a.m), _mm_andnot_ps(mask.m, b.m));
#endif
}

FORCE_INLINE int simd_movemask(const SimdFloat4& a) { return _mm_movemask_ps(a.m); }

FORCE_INLINE float simd_hmin(const SimdFloat4& a) {
    __m128 t1 = _mm_movehl_ps(a.m, a.m);
    __m128 t2 = _mm_min_ps(a.m, t1);
    __m128 t3 = _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(0, 0, 0, 1));
    return _mm_cvtss_f32(_mm_min_ss(t2, t3));
}

FORCE_INLINE float simd_hmax(const SimdFloat4& a) {
    __m128 t1 = _mm_movehl_ps(a.m, a.m);
    __m128 t2 = _mm_max_ps(a.m, t1);
    __m128 t3 = _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(0, 0, 0, 1));
    return _mm_cvtss_f32(_mm_max_ss(t2, t3));
}

#ifdef __AVX2__
struct ALIGN(32) SimdFloat8 {
    __m256 m;

    FORCE_INLINE SimdFloat8() : m(_mm256_setzero_ps()) {}
    FORCE_INLINE SimdFloat8(__m256 v) : m(v) {}
    FORCE_INLINE SimdFloat8(float f) : m(_mm256_set1_ps(f)) {}
    FORCE_INLINE SimdFloat8(const float* v) : m(_mm256_loadu_ps(v)) {}
    FORCE_INLINE SimdFloat8(float a, float b, float c, float d, float e, float f, float g, float h) 
        : m(_mm256_set_ps(h, g, f, e, d, c, b, a)) {}

    FORCE_INLINE SimdFloat8 operator+(const SimdFloat8& r) const { return _mm256_add_ps(m, r.m); }
    FORCE_INLINE SimdFloat8 operator-(const SimdFloat8& r) const { return _mm256_sub_ps(m, r.m); }
    FORCE_INLINE SimdFloat8 operator*(const SimdFloat8& r) const { return _mm256_mul_ps(m, r.m); }
    FORCE_INLINE SimdFloat8 operator/(const SimdFloat8& r) const { return _mm256_div_ps(m, r.m); }
    
    FORCE_INLINE SimdFloat8 operator&(const SimdFloat8& r) const { return _mm256_and_ps(m, r.m); }
    FORCE_INLINE SimdFloat8 operator|(const SimdFloat8& r) const { return _mm256_or_ps(m, r.m); }
    FORCE_INLINE SimdFloat8 operator^(const SimdFloat8& r) const { return _mm256_xor_ps(m, r.m); }

    FORCE_INLINE SimdFloat8 operator<(const SimdFloat8& r) const { return _mm256_cmp_ps(m, r.m, _CMP_LT_OQ); }
    FORCE_INLINE SimdFloat8 operator>(const SimdFloat8& r) const { return _mm256_cmp_ps(m, r.m, _CMP_GT_OQ); }
    FORCE_INLINE SimdFloat8 operator<=(const SimdFloat8& r) const { return _mm256_cmp_ps(m, r.m, _CMP_LE_OQ); }
    FORCE_INLINE SimdFloat8 operator>=(const SimdFloat8& r) const { return _mm256_cmp_ps(m, r.m, _CMP_GE_OQ); }
    FORCE_INLINE SimdFloat8 operator==(const SimdFloat8& r) const { return _mm256_cmp_ps(m, r.m, _CMP_EQ_OQ); }
    FORCE_INLINE SimdFloat8 operator!=(const SimdFloat8& r) const { return _mm256_cmp_ps(m, r.m, _CMP_NEQ_OQ); }
};

FORCE_INLINE SimdFloat8 simd8_min(const SimdFloat8& a, const SimdFloat8& b) { return _mm256_min_ps(a.m, b.m); }
FORCE_INLINE SimdFloat8 simd8_max(const SimdFloat8& a, const SimdFloat8& b) { return _mm256_max_ps(a.m, b.m); }
FORCE_INLINE SimdFloat8 simd8_blend(const SimdFloat8& mask, const SimdFloat8& a, const SimdFloat8& b) { return _mm256_blendv_ps(b.m, a.m, mask.m); }
FORCE_INLINE int simd8_movemask(const SimdFloat8& a) { return _mm256_movemask_ps(a.m); }

FORCE_INLINE float simd8_hmin(const SimdFloat8& a) {
    __m128 v = _mm_min_ps(_mm256_castps256_ps128(a.m), _mm256_extractf128_ps(a.m, 1));
    return simd_hmin(SimdFloat4(v));
}

FORCE_INLINE float simd8_hmax(const SimdFloat8& a) {
    __m128 v = _mm_max_ps(_mm256_castps256_ps128(a.m), _mm256_extractf128_ps(a.m, 1));
    return simd_hmax(SimdFloat4(v));
}

#else
struct ALIGN(32) SimdFloat8 {
    SimdFloat4 lo;
    SimdFloat4 hi;

    FORCE_INLINE SimdFloat8() : lo(), hi() {}
    FORCE_INLINE SimdFloat8(float f) : lo(f), hi(f) {}
    FORCE_INLINE SimdFloat8(const float* p) : lo(p), hi(p+4) {}
    FORCE_INLINE SimdFloat8(float a, float b, float c, float d, float e, float f, float g, float h) 
        : lo(a,b,c,d), hi(e,f,g,h) {}
    
    FORCE_INLINE SimdFloat8 operator+(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo + r.lo; o.hi = hi + r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator-(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo - r.lo; o.hi = hi - r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator*(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo * r.lo; o.hi = hi * r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator/(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo / r.lo; o.hi = hi / r.hi; return o; }
    
    FORCE_INLINE SimdFloat8 operator&(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo & r.lo; o.hi = hi & r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator|(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo | r.lo; o.hi = hi | r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator^(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo ^ r.lo; o.hi = hi ^ r.hi; return o; }
    
    FORCE_INLINE SimdFloat8 operator<(const SimdFloat8& r) const  { SimdFloat8 o; o.lo = lo < r.lo; o.hi = hi < r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator>(const SimdFloat8& r) const  { SimdFloat8 o; o.lo = lo > r.lo; o.hi = hi > r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator<=(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo <= r.lo; o.hi = hi <= r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator>=(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo >= r.lo; o.hi = hi >= r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator==(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo == r.lo; o.hi = hi == r.hi; return o; }
    FORCE_INLINE SimdFloat8 operator!=(const SimdFloat8& r) const { SimdFloat8 o; o.lo = lo != r.lo; o.hi = hi != r.hi; return o; }
};

FORCE_INLINE SimdFloat8 simd8_min(const SimdFloat8& a, const SimdFloat8& b) {
    SimdFloat8 o; o.lo = simd_min(a.lo, b.lo); o.hi = simd_min(a.hi, b.hi); return o; 
}
FORCE_INLINE SimdFloat8 simd8_max(const SimdFloat8& a, const SimdFloat8& b) {
    SimdFloat8 o; o.lo = simd_max(a.lo, b.lo); o.hi = simd_max(a.hi, b.hi); return o; 
}
FORCE_INLINE SimdFloat8 simd8_blend(const SimdFloat8& m, const SimdFloat8& a, const SimdFloat8& b) {
    SimdFloat8 o; o.lo = simd_blend(m.lo, a.lo, b.lo); o.hi = simd_blend(m.hi, a.hi, b.hi); return o;
}
FORCE_INLINE int simd8_movemask(const SimdFloat8& a) {
    return simd_movemask(a.lo) | (simd_movemask(a.hi) << 4);
}
FORCE_INLINE float simd8_hmin(const SimdFloat8& a) {
    return fast_min(simd_hmin(a.lo), simd_hmin(a.hi));
}
FORCE_INLINE float simd8_hmax(const SimdFloat8& a) {
    return fast_max(simd_hmax(a.lo), simd_hmax(a.hi));
}
#endif

FORCE_INLINE void prefetch_l1(const void* ptr) {
    _mm_prefetch((const char*)ptr, _MM_HINT_T0);
}

#endif