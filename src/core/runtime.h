#pragma once
#ifndef RT_RUNTIME_H
#define RT_RUNTIME_H

#ifdef _WIN32
#  include <windows.h>

/** Global HINSTANCE set by WinMain before calling main(). */
extern HINSTANCE g_hInstance;

#endif // _WIN32

#ifndef _SIZE_T_DEFINED
#  define _SIZE_T_DEFINED
typedef decltype(sizeof(0)) size_t;
#endif

#ifdef __cplusplus
namespace std {
    enum class align_val_t : size_t {};
    struct nothrow_t { explicit nothrow_t() = default; };
    extern const nothrow_t nothrow;
}

#pragma push_macro("new")
#undef new
inline void* operator new(size_t, void* ptr) noexcept { return ptr; }
inline void* operator new[](size_t, void* ptr) noexcept { return ptr; }
inline void  operator delete(void*, void*) noexcept {}
inline void  operator delete[](void*, void*) noexcept {}
#pragma pop_macro("new")

extern "C" {
#endif

/** Initializes the engine runtime (platform hooks and crash handlers). */
void runtime_init();

/** Aborts execution with a formatted fatal message and crash card. */
void panic_abort(const char* fmt, ...);

/** Scans active allocations and prints a detailed leak report with filenames, lines, and callstacks. */
void mem_report_leaks();

/** Explicitly tracked C-style allocation functions */
void* tracked_malloc(size_t size, const char* file, int line);
void* tracked_calloc(size_t num, size_t size, const char* file, int line);
void* tracked_realloc(void* ptr, size_t size, const char* file, int line);
void  tracked_free(void* ptr);

#ifdef __cplusplus
}
#endif

// ── Debug Overloaded Operator New Declarations ────────────────────────────────
#if defined(_DEBUG) || !defined(NDEBUG)
void* operator new(size_t size, const char* file, int line);
void* operator new[](size_t size, const char* file, int line);
void* operator new(size_t size, std::align_val_t al, const char* file, int line);
void* operator new[](size_t size, std::align_val_t al, const char* file, int line);

void operator delete(void* p, const char* file, int line) noexcept;
void operator delete[](void* p, const char* file, int line) noexcept;
void operator delete(void* p, std::align_val_t al, const char* file, int line) noexcept;
void operator delete[](void* p, std::align_val_t al, const char* file, int line) noexcept;

#ifndef TRACKED_NEW
#  define TRACKED_NEW new(__FILE__, __LINE__)
#endif
#endif

// ── rt_mutex_t ────────────────────────────────────────────────────────────────
#ifdef _WIN32
#define RT_MUTEX_SENTINEL 0xBEEFCAFE

struct rt_mutex_t {
    CRITICAL_SECTION cs;
    unsigned int     sentinel;
};

#ifdef __cplusplus
extern "C" {
#endif
int pthread_mutex_init(rt_mutex_t* m, const void* attr);
int pthread_mutex_lock(rt_mutex_t* m);
int pthread_mutex_unlock(rt_mutex_t* m);
int pthread_mutex_destroy(rt_mutex_t* m);
#ifdef __cplusplus
}
#endif

#endif // _WIN32

#endif // RT_RUNTIME_H