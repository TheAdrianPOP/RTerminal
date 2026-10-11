#pragma once
#ifndef RT_INTERNAL_H
#define RT_INTERNAL_H

#include "runtime.h"

// ── Self-Contained Fundamental Types ──────────────────────────────────────────
#ifndef _SIZE_T_DEFINED
#  define _SIZE_T_DEFINED
typedef decltype(sizeof(0))              size_t;
#endif
#ifndef _PTRDIFF_T_DEFINED
#  define _PTRDIFF_T_DEFINED
typedef decltype((char*)0 - (char*)0)    ptrdiff_t;
#endif
#ifndef _UINTPTR_T_DEFINED
#  define _UINTPTR_T_DEFINED
typedef decltype(sizeof(0))              uintptr_t;
#endif
#ifndef _INTPTR_T_DEFINED
#  define _INTPTR_T_DEFINED
typedef decltype((char*)0 - (char*)0)    intptr_t;
#endif
#ifndef _NULLPTR_T_DEFINED
#  define _NULLPTR_T_DEFINED
typedef decltype(nullptr)                nullptr_t;
#endif

#if !defined(_GCC_MAX_ALIGN_T) && !defined(_MAX_ALIGN_T) && !defined(_MAX_ALIGN_T_DEFINED)
#  define _GCC_MAX_ALIGN_T
#  define _MAX_ALIGN_T
#  define _MAX_ALIGN_T_DEFINED
typedef struct {
    long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
    long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;
#endif

typedef signed char          int8_t;
typedef short                int16_t;
typedef int                  int32_t;
typedef long long            int64_t;
typedef unsigned char        uint8_t;
typedef unsigned short       uint16_t;
typedef unsigned int         uint32_t;
typedef unsigned long long   uint64_t;

// ── Limits Constants ──────────────────────────────────────────────────────────
#ifndef INT_MAX
#  define INT_MAX      2147483647
#endif
#ifndef INT_MIN
#  define INT_MIN      (-2147483647 - 1)
#endif
#ifndef UINT_MAX
#  define UINT_MAX     0xFFFFFFFFU
#endif
#ifndef SIZE_MAX
#  define SIZE_MAX     (~((size_t)0))
#endif
#ifndef LONG_MAX
#  define LONG_MAX     2147483647L
#endif
#ifndef LONG_MIN
#  define LONG_MIN     (-2147483647L - 1L)
#endif
#ifndef LLONG_MAX
#  define LLONG_MAX    9223372036854775807LL
#endif
#ifndef LLONG_MIN
#  define LLONG_MIN    (-9223372036854775807LL - 1LL)
#endif
#ifndef ULLONG_MAX
#  define ULLONG_MAX   0xFFFFFFFFFFFFFFFFULL
#endif
#ifndef ERANGE
#  define ERANGE       34
#endif

// ── Stdio Forward Declarations ────────────────────────────────────────────────
#ifndef _FILE_DEFINED
#  define _FILE_DEFINED
typedef struct _iobuf FILE;
#endif

#ifndef SEEK_SET
#  define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#  define SEEK_CUR 1
#endif
#ifndef SEEK_END
#  define SEEK_END 2
#endif

extern "C" {
    FILE* __cdecl __acrt_iob_func(unsigned int index);
}

#ifndef stdin
#  define stdin  (__acrt_iob_func(0))
#endif
#ifndef stdout
#  define stdout (__acrt_iob_func(1))
#endif
#ifndef stderr
#  define stderr (__acrt_iob_func(2))
#endif

// ── Character & String Utilities ──────────────────────────────────────────────
inline int rt_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

inline int rt_toupper(int c) {
    return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c;
}

inline bool rt_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline int rt_stricmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        int c1 = rt_tolower((unsigned char)*s1);
        int c2 = rt_tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

// ── Assert & Panic ────────────────────────────────────────────────────────────
#ifndef rt_assert
#  ifdef NDEBUG
#    define rt_assert(expr) ((void)0)
#  else
#    define rt_assert(expr) ((expr) ? (void)0 : panic_abort("Assertion failed: %s (%s:%d)", #expr, __FILE__, __LINE__))
#  endif
#endif
#ifndef assert
#  define assert(expr) rt_assert(expr)
#endif

// ── Variadic Arguments ────────────────────────────────────────────────────────
#ifndef va_start
#  if defined(__GNUC__) || defined(__clang__)
     typedef __builtin_va_list va_list;
#    define va_start(v, l)   __builtin_va_start(v, l)
#    define va_end(v)        __builtin_va_end(v)
#    define va_arg(v, l)     __builtin_va_arg(v, l)
#    define va_copy(d, s)    __builtin_va_copy(d, s)
#  elif defined(_MSC_VER)
     typedef char* va_list;
#    define _INTSIZEOF(n)    ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))
#    define va_start(ap, v)  ((void)(ap = (va_list)&v + _INTSIZEOF(v)))
#    define va_arg(ap, t)    (*(t*)((ap += _INTSIZEOF(t)) - _INTSIZEOF(t)))
#    define va_end(ap)       ((void)(ap = (va_list)0))
#    define va_copy(d, s)    ((void)(d = s))
#  endif
#endif

// ── Signals ───────────────────────────────────────────────────────────────────
#ifndef _SIG_FN_T_DEFINED
#  define _SIG_FN_T_DEFINED
typedef void (__cdecl *_sig_fn_t)(int);
#endif
#ifndef SIGINT
#  define SIGINT 2
#endif
#ifndef SIG_DFL
#  define SIG_DFL ((_sig_fn_t)0)
#endif
#ifndef SIG_IGN
#  define SIG_IGN ((_sig_fn_t)1)
#endif
#ifndef SIG_ERR
#  define SIG_ERR ((_sig_fn_t)-1)
#endif
typedef int sig_atomic_t;

// ── Intrinsics, Allocation & Custom Formatter ─────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif

// ── Block system <stdio.h> and <cstdio> from ever loading ───────────────────
#ifndef _INC_STDIO
#  define _INC_STDIO
#endif
#ifndef _STDIO_H_
#  define _STDIO_H_
#endif
#ifndef _STDIO_H
#  define _STDIO_H
#endif
#ifndef _STDIO_DEFINED
#  define _STDIO_DEFINED
#endif
#ifndef _CSTDIO_
#  define _CSTDIO_
#endif
#ifndef _GLIBCXX_CSTDIO
#  define _GLIBCXX_CSTDIO 1
#endif

extern "C" {
    // Exact, non-overloaded string & memory intrinsics
    size_t __cdecl strlen(const char* str);
    int    __cdecl strcmp(const char* s1, const char* s2);
    int    __cdecl strncmp(const char* s1, const char* s2, size_t n);
    char*  __cdecl strchr(const char* s, int c);
    char*  __cdecl strrchr(const char* s, int c);
    char*  __cdecl strstr(const char* haystack, const char* needle);
    void*  __cdecl memcpy(void* dest, const void* src, size_t count);
    void*  __cdecl memmove(void* dest, const void* src, size_t count);
    void*  __cdecl memset(void* dest, int val, size_t count);
    int    __cdecl memcmp(const void* buf1, const void* buf2, size_t count);

    // Allocations
    void*  __cdecl malloc(size_t size);
    void   __cdecl free(void* ptr);
    void*  __cdecl realloc(void* ptr, size_t size);
    void*  __cdecl calloc(size_t num, size_t size);
    void*  __cdecl _aligned_malloc(size_t size, size_t alignment);
    void   __cdecl _aligned_free(void* ptr);

    // Stdio file primitives (implemented in stdio.cpp)
    FILE*  __cdecl fopen(const char* filename, const char* mode);
    int    __cdecl fclose(FILE* stream);
    char*  __cdecl fgets(char* str, int count, FILE* stream);
    int    __cdecl fputs(const char* str, FILE* stream);
    size_t __cdecl fread(void* ptr, size_t size, size_t count, FILE* stream);
    size_t __cdecl fwrite(const void* ptr, size_t size, size_t count, FILE* stream);
    int    __cdecl fseek(FILE* stream, long offset, int origin);
    long   __cdecl ftell(FILE* stream);
    int    __cdecl fflush(FILE* stream);
    int    __cdecl feof(FILE* stream);
    int    __cdecl ferror(FILE* stream);
    int    __cdecl remove(const char* filename);
    void   __cdecl rewind(FILE* stream);

    // Standard Formatted I/O (pure C, zero-CRT, implemented in stdio.cpp)
    int    __cdecl vsnprintf(char* buf, size_t count, const char* fmt, va_list args);
    int    __cdecl snprintf(char* buf, size_t count, const char* fmt, ...);
    int    __cdecl vfprintf(FILE* stream, const char* fmt, va_list args);
    int    __cdecl fprintf(FILE* stream, const char* fmt, ...);
    int    __cdecl printf(const char* fmt, ...);
    int    __cdecl vprintf(const char* fmt, va_list args);

    // Signals
    _sig_fn_t __cdecl signal(int sig, _sig_fn_t handler);

    extern int g_crt_errno;
}

// Backward-compatibility aliases (in case old engine code still calls rt_*)
#define rt_vsnprintf vsnprintf
#define rt_snprintf  snprintf
#define rt_fprintf   fprintf
#define rt_printf    printf

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <intrin.h>

#  ifdef _environ
#    undef _environ
#  endif
#  ifdef environ
#    undef environ
#  endif

extern "C" {
struct LocalFile {
    HANDLE hFile;
    bool   isConsole;
    bool   eof;
    bool   err;
};
extern LocalFile g_std_files[3];
}

#define DEFINE_STRING_SHIM(ret, name, ...) \
    ret name(__VA_ARGS__); \
    decltype(&name) __imp_##name = name; \
    ret name(__VA_ARGS__)

#endif // _WIN32

#endif // RT_INTERNAL_H