// src/engine/core/runtime.cpp — Windows: custom heap + MinGW shims. POSIX: libc allocators.
#include "runtime.h"
#include <cstddef>
#include <new>
#include <cstdarg>
#include <malloc.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <intrin.h>

// ── Global hInstance (needed for CreateWindow etc.) ──────────────────────────
HINSTANCE g_hInstance = NULL;

// ── panic_abort ───────────────────────────────────────────────────────────────
extern "C" void panic_abort(const char* fmt, ...) {
    char message[512] = "Fatal Error: ";
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        wvsprintfA(message + 13, fmt, args);   // Win32 only — no libmingwex
        va_end(args);
    } else {
        lstrcatA(message, "Unknown engine error.");
    }

#ifdef _DEBUG
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
    __debugbreak();
#else
    HANDLE hFile = CreateFileA("crash_log.txt", GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, message, (DWORD)lstrlenA(message), &written, NULL);
        WriteFile(hFile, "\r\n\r\nStack Trace (Addresses):\r\n", 30, &written, NULL);

        void* stack[32];
        unsigned short frames = CaptureStackBackTrace(1, 32, stack, NULL);
        for (unsigned short i = 0; i < frames; i++) {
            char addrBuf[64];
            wsprintfA(addrBuf, "  [%d] 0x%p\r\n", i, stack[i]);
            WriteFile(hFile, addrBuf, (DWORD)lstrlenA(addrBuf), &written, NULL);
        }
        CloseHandle(hFile);
    }

    char finalMsg[1024];
    wsprintfA(finalMsg,
              "%s\n\nA crash log has been saved to 'crash_log.txt'.", message);
    MessageBoxA(NULL, finalMsg, "RTerminal Engine Panic",
                MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
#endif
    TerminateProcess(GetCurrentProcess(), 0xDEADBEEF);
}

// ── SEH Exception Filter ──────────────────────────────────────────────────────
static LONG WINAPI engine_exception_filter(EXCEPTION_POINTERS* ep) {
    const char* reason = "Unknown Exception";
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:         reason = "Access Violation";          break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    reason = "Array Bounds Exceeded";     break;
        case EXCEPTION_BREAKPOINT:               reason = "Breakpoint";                break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:    reason = "Datatype Misalignment";     break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:     reason = "FP Denormal Operand";       break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       reason = "FP Divide by Zero";         break;
        case EXCEPTION_FLT_INEXACT_RESULT:       reason = "FP Inexact Result";         break;
        case EXCEPTION_FLT_INVALID_OPERATION:    reason = "FP Invalid Operation";      break;
        case EXCEPTION_FLT_OVERFLOW:             reason = "FP Overflow";               break;
        case EXCEPTION_FLT_STACK_CHECK:          reason = "FP Stack Check";            break;
        case EXCEPTION_FLT_UNDERFLOW:            reason = "FP Underflow";              break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:      reason = "Illegal Instruction";       break;
        case EXCEPTION_IN_PAGE_ERROR:            reason = "In-page Error";             break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       reason = "Integer Divide by Zero";    break;
        case EXCEPTION_INT_OVERFLOW:             reason = "Integer Overflow";          break;
        case EXCEPTION_INVALID_DISPOSITION:      reason = "Invalid Disposition";       break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: reason = "Non-continuable Exception"; break;
        case EXCEPTION_PRIV_INSTRUCTION:         reason = "Privileged Instruction";    break;
        case EXCEPTION_SINGLE_STEP:              reason = "Single Step";               break;
        case EXCEPTION_STACK_OVERFLOW:           reason = "Stack Overflow";            break;
    }
    panic_abort("Hardware Crash: %s (0x%08X) at 0x%p",
                reason,
                ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

void runtime_init() {
    g_hInstance = GetModuleHandleA(NULL);
    SetUnhandledExceptionFilter(engine_exception_filter);

#ifdef _DEBUG
    if (AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$",  "r", stdin);
    }
#endif
}

// ── Debug Leak Tracker ────────────────────────────────────────────────────────
#ifdef _DEBUG
static volatile long      g_active_allocs = 0;
static volatile long long g_active_bytes  = 0;

struct AllocHeader { size_t size; size_t magic; };
constexpr size_t ALLOC_MAGIC = 0xDEADC0DE;

extern "C" void mem_report_leaks() {
    if (g_active_allocs != 0 || g_active_bytes != 0) {
        char buf[128];
        wsprintfA(buf, "\n[MEMORY] LEAK DETECTED: %ld allocations, %lld bytes.\n",
                  g_active_allocs, g_active_bytes);
        OutputDebugStringA(buf);
    } else {
        OutputDebugStringA("\n[MEMORY] No leaks detected.\n");
    }
}
#endif // _DEBUG

// ── Heap Allocation ───────────────────────────────────────────────────────────
static inline void* heap_alloc(size_t n) {
#ifdef _DEBUG
    size_t total = n + sizeof(AllocHeader);
    void* p = HeapAlloc(GetProcessHeap(), 0, total ? total : 1);
    if (!p) panic_abort("HeapAlloc failed for %zu bytes", n);
    AllocHeader* h = static_cast<AllocHeader*>(p);
    h->size = n; h->magic = ALLOC_MAGIC;
    InterlockedIncrement(&g_active_allocs);
    InterlockedAdd64(&g_active_bytes, (LONGLONG)n);
    return static_cast<void*>(h + 1);
#else
    void* p = HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
    if (!p) panic_abort("HeapAlloc failed");
    return p;
#endif
}

static inline void heap_free(void* p) {
    if (!p) return;
#ifdef _DEBUG
    AllocHeader* h = static_cast<AllocHeader*>(p) - 1;
    if (h->magic != ALLOC_MAGIC)
        panic_abort("Heap corruption detected (magic mismatch)");
    InterlockedDecrement(&g_active_allocs);
    InterlockedAdd64(&g_active_bytes, -(LONGLONG)h->size);
    h->magic = 0;
    HeapFree(GetProcessHeap(), 0, h);
#else
    HeapFree(GetProcessHeap(), 0, p);
#endif
}

static inline void* aligned_alloc_impl(size_t n, size_t al) {
    void* p = _aligned_malloc(n ? n : 1, al);
    if (!p) panic_abort("aligned_alloc failed for %zu bytes", n);
#ifdef _DEBUG
    InterlockedIncrement(&g_active_allocs);
    InterlockedAdd64(&g_active_bytes, (LONGLONG)n);
#endif
    return p;
}

static inline void aligned_free_impl(void* p) {
    if (!p) return;
#ifdef _DEBUG
    InterlockedDecrement(&g_active_allocs);
#endif
    _aligned_free(p);
}

// ── operator new / delete ─────────────────────────────────────────────────────
void* operator new(size_t n)                        { return heap_alloc(n); }
void* operator new[](size_t n)                      { return heap_alloc(n); }
void* operator new(size_t n, std::align_val_t al)   { return aligned_alloc_impl(n, (size_t)al); }
void* operator new[](size_t n, std::align_val_t al) { return aligned_alloc_impl(n, (size_t)al); }

void operator delete(void* p) noexcept                          { heap_free(p); }
void operator delete[](void* p) noexcept                        { heap_free(p); }
void operator delete(void* p, size_t) noexcept                  { heap_free(p); }
void operator delete[](void* p, size_t) noexcept                { heap_free(p); }
void operator delete(void* p, std::align_val_t) noexcept        { aligned_free_impl(p); }
void operator delete[](void* p, std::align_val_t) noexcept      { aligned_free_impl(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept   { aligned_free_impl(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { aligned_free_impl(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept   { heap_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { heap_free(p); }

// ── __cxa_guard (Thread-safe Statics) ────────────────────────────────────────
static inline BYTE atomic_load_byte(volatile BYTE* p) { return *p; }

static inline BYTE atomic_cas_byte(volatile BYTE* p, BYTE desired, BYTE expected) {
    ULONG_PTR addr    = reinterpret_cast<ULONG_PTR>(p);
    ULONG_PTR aligned = addr & ~(ULONG_PTR)3;
    int       shift   = (int)((addr - aligned) * 8);
    volatile LONG* word = reinterpret_cast<volatile LONG*>(aligned);
    LONG old_word = *word;
    while (true) {
        BYTE old_byte = (BYTE)((old_word >> shift) & 0xFF);
        if (old_byte != expected) return old_byte;
        LONG mask     = (LONG)0xFF << shift;
        LONG new_word = (old_word & ~mask) | ((LONG)desired << shift);
        LONG prev     = InterlockedCompareExchange(word, new_word, old_word);
        if (prev == old_word) return expected;
        old_word = prev;
    }
}

static inline void atomic_store_byte(volatile BYTE* p, BYTE val) {
    ULONG_PTR addr    = reinterpret_cast<ULONG_PTR>(p);
    ULONG_PTR aligned = addr & ~(ULONG_PTR)3;
    int       shift   = (int)((addr - aligned) * 8);
    volatile LONG* word = reinterpret_cast<volatile LONG*>(aligned);
    LONG mask = (LONG)0xFF << shift;
    LONG old_word = *word;
    while (true) {
        LONG new_word = (old_word & ~mask) | ((LONG)val << shift);
        LONG prev     = InterlockedCompareExchange(word, new_word, old_word);
        if (prev == old_word) return;
        old_word = prev;
    }
}

extern "C" int __cxa_guard_acquire(long long* guard) {
    volatile BYTE* flag      = reinterpret_cast<volatile BYTE*>(guard);
    volatile BYTE* lock_byte = reinterpret_cast<volatile BYTE*>(guard) + 1;
    if (atomic_load_byte(flag) & 0x1) return 0;
    while (atomic_cas_byte(lock_byte, 1, 0) != 0) {
        if (atomic_load_byte(flag) & 0x1) return 0;
        YieldProcessor(); SwitchToThread();
    }
    if (atomic_load_byte(flag) & 0x1) { atomic_store_byte(lock_byte, 0); return 0; }
    return 1;
}

extern "C" void __cxa_guard_release(long long* guard) {
    volatile BYTE* flag      = reinterpret_cast<volatile BYTE*>(guard);
    volatile BYTE* lock_byte = reinterpret_cast<volatile BYTE*>(guard) + 1;
    atomic_store_byte(flag, 0x1);
    atomic_store_byte(lock_byte, 0);
}

extern "C" void __cxa_guard_abort(long long* guard) {
    volatile BYTE* lock_byte = reinterpret_cast<volatile BYTE*>(guard) + 1;
    atomic_store_byte(lock_byte, 0);
}

// ── pthread TLS ───────────────────────────────────────────────────────────────
#define RT_MAX_KEYS 256
static DWORD         rt_tls_slots[RT_MAX_KEYS];
static volatile long rt_tls_count = 0;
static volatile long rt_tls_lock  = 0;

extern "C" {

int pthread_key_create(unsigned* key, void (*/*dtor*/)(void*)) {
    while (InterlockedCompareExchange(&rt_tls_lock, 1, 0) != 0) YieldProcessor();
    int k = (int)rt_tls_count;
    if (k < RT_MAX_KEYS) {
        rt_tls_slots[k] = TlsAlloc();
        rt_tls_count++;
        *key = (unsigned)k;
        InterlockedExchange(&rt_tls_lock, 0);
        return 0;
    }
    InterlockedExchange(&rt_tls_lock, 0);
    panic_abort("Too many TLS keys!");
    return 1;
}

void* pthread_getspecific(unsigned key) {
    if (key >= (unsigned)rt_tls_count) return nullptr;
    return TlsGetValue(rt_tls_slots[key]);
}

int pthread_setspecific(unsigned key, const void* val) {
    if (key >= (unsigned)rt_tls_count) return 1;
    return TlsSetValue(rt_tls_slots[key], (LPVOID)val) ? 0 : 1;
}

// ── pthread Mutex ─────────────────────────────────────────────────────────────
int pthread_mutex_init(rt_mutex_t* m, const void* /*attr*/) {
    if (!m) return 1;
    InitializeCriticalSection(&m->cs);
    m->sentinel = RT_MUTEX_SENTINEL;
    return 0;
}
int pthread_mutex_lock(rt_mutex_t* m) {
    if (!m || m->sentinel != RT_MUTEX_SENTINEL)
        panic_abort("pthread_mutex_lock: invalid or destroyed mutex");
    EnterCriticalSection(&m->cs); return 0;
}
int pthread_mutex_unlock(rt_mutex_t* m) {
    if (!m || m->sentinel != RT_MUTEX_SENTINEL)
        panic_abort("pthread_mutex_unlock: invalid or destroyed mutex");
    LeaveCriticalSection(&m->cs); return 0;
}
int pthread_mutex_destroy(rt_mutex_t* m) {
    if (!m || m->sentinel != RT_MUTEX_SENTINEL)
        panic_abort("pthread_mutex_destroy: invalid or already-destroyed mutex");
    DeleteCriticalSection(&m->cs); m->sentinel = 0; return 0;
}

unsigned long pthread_self()  { return GetCurrentThreadId(); }
int pthread_once(long* control, void (*init)(void)) {
    if (InterlockedCompareExchange(control, 1, 0) == 0) init();
    return 0;
}

// ── MinGW __gthrw wrappers ────────────────────────────────────────────────────
int   __gthrw_pthread_active_p()                                { return 1; }
int   __gthrw___pthread_key_create(void* k, void(*d)(void*))    { return pthread_key_create((unsigned*)k, d); }
void* __gthrw___pthread_getspecific(unsigned k)                 { return pthread_getspecific(k); }
int   __gthrw___pthread_setspecific(unsigned k, const void* v)  { return pthread_setspecific(k, v); }
int   __gthrw___pthread_mutex_lock(void* m)                     { return pthread_mutex_lock((rt_mutex_t*)m); }
int   __gthrw___pthread_mutex_unlock(void* m)                   { return pthread_mutex_unlock((rt_mutex_t*)m); }

// ── MinGW stubs ───────────────────────────────────────────────────────────────
void __main() {}

void* __mingw_aligned_malloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }
void  __mingw_aligned_free(void* ptr)                       { _aligned_free(ptr); }

// ── Printf / snprintf replacements — zero libmingwex dependency ──────────────
//
// MinGW generates calls to __mingw_printf / __mingw_fprintf / __mingw_snprintf
// / __mingw_vsnprintf instead of the C standard names.  The default MinGW
// implementations delegate to their own formatting engine (mingw_pformat +
// gdtoa) which costs ~37 KB.
//
// Strategy: format into a stack buffer using _vsnprintf (UCRT underscore form,
// no MinGW layer, supports %f/%g/%e), then write with WriteFile.  No FILE*,
// no stdio buffering, no libmingwex.
//
// _vsnprintf declared here explicitly to avoid pulling in <stdio.h> and
// accidentally triggering MinGW's macro replacements.

extern "C" int __cdecl _vsnprintf(char*, size_t, const char*, va_list);

// Shared low-level output helpers.
static void rt_out(DWORD handle, const char* buf, int len) {
    if (len <= 0) return;
    DWORD w; WriteFile(GetStdHandle(handle), buf, (DWORD)len, &w, nullptr);
}

// Core: format → stack buffer → WriteFile.  2 KB covers all HUD snprintf calls.
static int rt_vprintf_impl(DWORD handle, const char* fmt, va_list args) {
    char buf[2048];
    int len = _vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    if (len < 0) len = (int)sizeof(buf) - 1;
    buf[len] = '\0';
    rt_out(handle, buf, len);
    return len;
}

// ── Public C entry points MinGW calls ────────────────────────────────────────

int __mingw_printf(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    int r = rt_vprintf_impl(STD_OUTPUT_HANDLE, fmt, args);
    va_end(args); return r;
}

int __mingw_fprintf(void* /*file*/, const char* fmt, ...) {
    // All fprintf in this codebase targets stderr (error/debug messages).
    // If your code uses fprintf(stdout, ...) anywhere, change STD_ERROR_HANDLE
    // to STD_OUTPUT_HANDLE for that call site, or inspect the file* argument.
    va_list args; va_start(args, fmt);
    int r = rt_vprintf_impl(STD_ERROR_HANDLE, fmt, args);
    va_end(args); return r;
}

int __mingw_snprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    int r = _vsnprintf(buf, n, fmt, args);
    va_end(args);
    if (r < 0 || (size_t)r >= n) buf[n - 1] = '\0';
    return r;
}

int __mingw_vsnprintf(char* buf, size_t n, const char* fmt, va_list args) {
    int r = _vsnprintf(buf, n, fmt, args);
    if (r < 0 || (size_t)r >= n) buf[n - 1] = '\0';
    return r;
}

// Satisfy any remaining direct references from object files.
int vprintf (const char* fmt, va_list args)              { return rt_vprintf_impl(STD_OUTPUT_HANDLE, fmt, args); }
int vfprintf(void* /*f*/, const char* fmt, va_list args) { return rt_vprintf_impl(STD_ERROR_HANDLE,  fmt, args); }
int vsnprintf(char* buf, size_t n, const char* fmt, va_list args) { return __mingw_vsnprintf(buf, n, fmt, args); }
int snprintf (char* buf, size_t n, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    int r = __mingw_vsnprintf(buf, n, fmt, args);
    va_end(args); return r;
}


// ── Soft-float / libgcc wrap stubs ───────────────────────────────────────────
//
// These symbols are referenced by libmingwex or libgcc objects pulled in
// transitively.  On i686+SSE2 every one of these is a single hardware
// instruction; the wrap just keeps the linker from dragging in libgcc's
// software implementations.
//
// Declared with __attribute__((noinline)) so the compiler never folds them
// into call sites in a way that could cause infinite recursion on targets
// that lack the relevant hardware instruction.

__attribute__((noinline)) double __wrap___adddf3   (double a, double b) { return a + b; }
__attribute__((noinline)) double __wrap___subdf3   (double a, double b) { return a - b; }
__attribute__((noinline)) double __wrap___muldf3   (double a, double b) { return a * b; }
__attribute__((noinline)) double __wrap___divdf3   (double a, double b) { return a / b; }
__attribute__((noinline)) double __wrap___extendsfdf2(float  x)         { return (double)x; }
__attribute__((noinline)) double __wrap___floatsidf2 (int    x)         { return (double)x; }

// Integer power — binary exponentiation, O(log n)
__attribute__((noinline)) double __wrap___powi(double x, int n) {
    double r = 1.0;
    bool neg = n < 0; if (neg) n = -n;
    for (; n; n >>= 1) { if (n & 1) r *= x; x *= x; }
    return neg ? 1.0 / r : r;
}
__attribute__((noinline)) float __wrap___powisf2(float x, int n) {
    float r = 1.f;
    bool neg = n < 0; if (neg) n = -n;
    for (; n; n >>= 1) { if (n & 1) r *= x; x *= x; }
    return neg ? 1.f / r : r;
}

// Unordered double comparison — returns nonzero if either arg is NaN.
// Used by code that calls std::isunordered or compares with NaN guards.
__attribute__((noinline)) int __wrap___unordf2(double a, double b) {
    return (a != a) | (b != b);   // NaN != NaN by IEEE-754
}

// Greater-than for long double / __float128.
// On 32-bit x86 __float128 is a library type; this wrap satisfies the
// linker reference without pulling in the 128-bit soft-float library.
__attribute__((noinline)) int __wrap___gttf2(long double a, long double b) {
    return a > b ? 1 : 0;
}

} // extern "C"
#endif // _WIN32