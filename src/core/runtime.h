#pragma once
#ifndef RT_RUNTIME_H
#define RT_RUNTIME_H

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

/** Global HINSTANCE set by WinMain before calling main().
 *  Use this anywhere you need to pass hInstance (e.g. CreateWindow, LoadIcon). */
extern HINSTANCE g_hInstance;

#endif // _WIN32

#ifdef __cplusplus
extern "C" {
#endif

/** Initializes the engine runtime (platform hooks).
 *  Windows: SEH filter, optional debug console, hInstance.
 *  Linux/POSIX: ignores SIGPIPE on writes to broken pipes. */
void runtime_init();

/** Aborts execution with a formatted fatal message.
 *  Windows debug: OutputDebugString + __debugbreak.
 *  Windows release: crash_log.txt + MessageBox + stack addresses.
 *  POSIX: stderr + optional crash_log.txt, then abort(). */
void panic_abort(const char* fmt, ...);

#ifdef _DEBUG
/** Reports memory leaks to the debug output.
 *  Call at the very end of main() in debug builds. */
void mem_report_leaks();
#else
/** No-op in release builds. */
static inline void mem_report_leaks() {}
#endif

#ifdef __cplusplus
}
#endif

// ── rt_mutex_t ────────────────────────────────────────────────────────────────
// Concrete mutex type backed by a Windows CRITICAL_SECTION.
// Pass a pointer to this type to pthread_mutex_init/lock/unlock/destroy.
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