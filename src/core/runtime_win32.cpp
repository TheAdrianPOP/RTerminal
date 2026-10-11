// runtime_win32.cpp — Zero-CRT Windows Runtime (Excluding Heap Memory & Stdio)

#ifndef _WIN32
#  error "runtime_win32.cpp is only intended for Windows targets."
#endif

#ifndef _CRTIMP
#  define _CRTIMP
#endif

#include "internal.h"

#ifdef _environ
#  undef _environ
#endif
#ifdef environ
#  undef environ
#endif

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif

// ── Shared CRT State ──────────────────────────────────────────────────────────
int g_crt_errno = 0;

extern "C" {

// Diagnostic panic abort declaration (defined in init.cpp)
__attribute__((noreturn)) void panic_abort(const char* fmt, ...);

int* __cdecl _errno(void) { return &g_crt_errno; }
int* (__cdecl *__imp__errno)(void) = &_errno;

// ── Memory Byte Operations ───────────────────────────────────────────────────
// Prevent GCC from replacing these loops with calls to memset/memcpy (infinite recursion)
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC push_options
#  pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

// Attributes ensuring LTO and --gc-sections NEVER discard these symbols:
#define MEM_PRIMITIVE_ATTR __attribute__((used, externally_visible, no_builtin))

extern "C" {

MEM_PRIMITIVE_ATTR void* memset(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    unsigned char  val = static_cast<unsigned char>(c);
    while (n--) *p++ = val;
    return s;
}
decltype(&memset) __imp_memset = &memset;

MEM_PRIMITIVE_ATTR void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    while (n--) *d++ = *s++;
    return dest;
}
decltype(&memcpy) __imp_memcpy = &memcpy;

MEM_PRIMITIVE_ATTR void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
decltype(&memmove) __imp_memmove = &memmove;

MEM_PRIMITIVE_ATTR int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}
decltype(&memcmp) __imp_memcmp = &memcmp;

} // extern "C"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC pop_options
#endif

// ── String Operations ─────────────────────────────────────────────────────────
DEFINE_STRING_SHIM(size_t, strlen, const char* s) {
    const char* p = s;
    while (*p) p++;
    return static_cast<size_t>(p - s);
}

DEFINE_STRING_SHIM(int, strcmp, const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

DEFINE_STRING_SHIM(int, strncmp, const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    while (n > 1 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

DEFINE_STRING_SHIM(char*, strcpy, char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

DEFINE_STRING_SHIM(char*, strncpy, char* dest, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

DEFINE_STRING_SHIM(char*, strcat, char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

DEFINE_STRING_SHIM(int, _stricmp, const char* s1, const char* s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : static_cast<unsigned char>(*s1);
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : static_cast<unsigned char>(*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : static_cast<unsigned char>(*s1);
    int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : static_cast<unsigned char>(*s2);
    return c1 - c2;
}

DEFINE_STRING_SHIM(int, _strnicmp, const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (n > 0 && *s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : static_cast<unsigned char>(*s1);
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : static_cast<unsigned char>(*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : static_cast<unsigned char>(*s1);
    int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : static_cast<unsigned char>(*s2);
    return c1 - c2;
}

int strcasecmp(const char* s1, const char* s2) { return _stricmp(s1, s2); }
decltype(&strcasecmp) __imp_strcasecmp = &strcasecmp;

int strncasecmp(const char* s1, const char* s2, size_t n) { return _strnicmp(s1, s2, n); }
decltype(&strncasecmp) __imp_strncasecmp = &strncasecmp;

DEFINE_STRING_SHIM(char*, strchr, const char* s, int c) {
    char ch = static_cast<char>(c);
    while (*s != ch) {
        if (!*s) return nullptr;
        s++;
    }
    return const_cast<char*>(s);
}

DEFINE_STRING_SHIM(char*, strrchr, const char* s, int c) {
    char ch = static_cast<char>(c);
    const char* last = nullptr;
    do {
        if (*s == ch) last = s;
    } while (*s++);
    return const_cast<char*>(last);
}

DEFINE_STRING_SHIM(char*, strstr, const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack + 1;
            const char* n = needle + 1;
            while (*h && *n && *h == *n) {
                h++; n++;
            }
            if (!*n) return const_cast<char*>(haystack);
        }
    }
    return nullptr;
}

// ── Character Classification ──────────────────────────────────────────────────
DEFINE_STRING_SHIM(int, tolower, int c) { return (c >= 'A' && c <= 'Z') ? (c + 32) : c; }
DEFINE_STRING_SHIM(int, toupper, int c) { return (c >= 'a' && c <= 'z') ? (c - 32) : c; }
DEFINE_STRING_SHIM(unsigned short, towupper, unsigned short c) { return (c >= L'a' && c <= L'z') ? (c - 32) : c; }
DEFINE_STRING_SHIM(unsigned short, towlower, unsigned short c) { return (c >= L'A' && c <= L'Z') ? (c + 32) : c; }

DEFINE_STRING_SHIM(int, isalnum, int c)  { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
DEFINE_STRING_SHIM(int, isalpha, int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
DEFINE_STRING_SHIM(int, isdigit, int c)  { return (c >= '0' && c <= '9'); }
DEFINE_STRING_SHIM(int, isspace, int c)  { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
DEFINE_STRING_SHIM(int, isxdigit, int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
DEFINE_STRING_SHIM(int, isprint, int c)  { return c >= 0x20 && c <= 0x7E; }
DEFINE_STRING_SHIM(int, isgraph, int c)  { return c > 0x20 && c <= 0x7E; }
DEFINE_STRING_SHIM(int, iscntrl, int c)  { return (c >= 0 && c < 0x20) || c == 0x7F; }
DEFINE_STRING_SHIM(int, ispunct, int c)  { return isgraph(c) && !isalnum(c); }

// ── String Conversions ────────────────────────────────────────────────────────
static long strtol_impl(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    if (!s) {
        if (endptr) *endptr = nullptr;
        return 0;
    }

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') s++;

    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if ((base == 0 || base == 16) && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0) {
        base = (*s == '0') ? 8 : 10;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = const_cast<char*>(nptr);
        return 0;
    }

    const unsigned long limit  = neg ? (static_cast<unsigned long>(LONG_MAX) + 1UL) : static_cast<unsigned long>(LONG_MAX);
    const unsigned long cutoff = limit / static_cast<unsigned long>(base);
    const int cutlim           = static_cast<int>(limit % static_cast<unsigned long>(base));

    unsigned long acc = 0;
    int any = 0;
    bool overflow = false;

    for (;; s++) {
        int c = static_cast<unsigned char>(*s);
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'a' && c <= 'z') c -= 'a' - 10;
        else if (c >= 'A' && c <= 'Z') c -= 'A' - 10;
        else break;

        if (c >= base) break;

        if (overflow || acc > cutoff || (acc == cutoff && c > cutlim)) {
            overflow = true;
        } else {
            any = 1;
            acc = acc * static_cast<unsigned long>(base) + static_cast<unsigned long>(c);
        }
    }

    if (endptr) *endptr = const_cast<char*>(any ? s : nptr);
    if (overflow) {
        g_crt_errno = ERANGE;
        return neg ? LONG_MIN : LONG_MAX;
    }
    return neg ? -static_cast<long>(acc) : static_cast<long>(acc);
}

long strtol(const char* nptr, char** endptr, int base) { return strtol_impl(nptr, endptr, base); }
decltype(&strtol) __imp_strtol = &strtol;

long __mingw_strtol(const char* nptr, char** endptr, int base) { return strtol_impl(nptr, endptr, base); }
decltype(&__mingw_strtol) __imp___mingw_strtol = &__mingw_strtol;

int atoi(const char* nptr) { return static_cast<int>(strtol_impl(nptr, nullptr, 10)); }
decltype(&atoi) __imp_atoi = &atoi;

long atol(const char* nptr) { return strtol_impl(nptr, nullptr, 10); }
decltype(&atol) __imp_atol = &atol;

// ── atexit / __cxa_atexit ─────────────────────────────────────────────────────
struct AtExitNode {
    void (*func)(void*);
    void*       arg;
    bool        is_cxa;
    AtExitNode* next;
};

static AtExitNode* volatile g_atexit_head = nullptr;

static int register_atexit(void (*func)(void*), void* arg, bool is_cxa) {
    if (!func) return -1;
    AtExitNode* node = static_cast<AtExitNode*>(HeapAlloc(GetProcessHeap(), 0, sizeof(AtExitNode)));
    if (!node) return -1;

    node->func   = func;
    node->arg    = arg;
    node->is_cxa = is_cxa;

    while (true) {
        AtExitNode* head = const_cast<AtExitNode*>(g_atexit_head);
        node->next = head;
        if (InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(&g_atexit_head),
                node,
                head) == head) {
            return 0;
        }
    }
}

int atexit(void (*func)(void)) {
    return register_atexit(reinterpret_cast<void (*)(void*)>(func), nullptr, false);
}
int (__cdecl *__imp_atexit)(void (*)(void)) = &atexit;

int __cdecl _crt_atexit(void (*func)(void)) { return atexit(func); }
int (__cdecl *__imp__crt_atexit)(void (*)(void)) = &_crt_atexit;

#ifndef _ONEXIT_T_DEFINED
#  define _ONEXIT_T_DEFINED
typedef int (__cdecl * _onexit_t)(void);
#endif

_onexit_t __cdecl _onexit(_onexit_t func) {
    if (atexit(reinterpret_cast<void (*)(void)>(func)) == 0) return func;
    return nullptr;
}
_onexit_t (__cdecl *__imp__onexit)(_onexit_t) = &_onexit;

int __cxa_atexit(void (*func)(void*), void* arg, void* /*dso_handle*/) {
    return register_atexit(func, arg, true);
}

void run_atexit_handlers(void) {
    HANDLE hHeap = GetProcessHeap();
    AtExitNode* curr = static_cast<AtExitNode*>(
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_atexit_head), nullptr)
    );
    while (curr) {
        AtExitNode* next = curr->next;
        if (curr->func) {
            if (curr->is_cxa) {
                curr->func(curr->arg);
            } else {
                reinterpret_cast<void (*)(void)>(curr->func)();
            }
        }
        HeapFree(hHeap, 0, curr);
        curr = next;
    }
}

// ── Thread-Safe Statics (__cxa_guard) ────────────────────────────────────────
static inline BYTE atomic_load_byte(volatile BYTE* p) { return *p; }

static inline BYTE atomic_cas_byte(volatile BYTE* p, BYTE desired, BYTE expected) {
    ULONG_PTR addr    = reinterpret_cast<ULONG_PTR>(p);
    ULONG_PTR aligned = addr & ~(ULONG_PTR)3;
    int       shift   = static_cast<int>((addr - aligned) * 8);
    volatile LONG* word = reinterpret_cast<volatile LONG*>(aligned);
    LONG old_word = *word;
    while (true) {
        BYTE old_byte = static_cast<BYTE>((old_word >> shift) & 0xFF);
        if (old_byte != expected) return old_byte;
        LONG mask     = static_cast<LONG>(0xFF) << shift;
        LONG new_word = (old_word & ~mask) | (static_cast<LONG>(desired) << shift);
        LONG prev     = InterlockedCompareExchange(word, new_word, old_word);
        if (prev == old_word) return expected;
        old_word = prev;
    }
}

static inline void atomic_store_byte(volatile BYTE* p, BYTE val) {
    ULONG_PTR addr    = reinterpret_cast<ULONG_PTR>(p);
    ULONG_PTR aligned = addr & ~(ULONG_PTR)3;
    int       shift   = static_cast<int>((addr - aligned) * 8);
    volatile LONG* word = reinterpret_cast<volatile LONG*>(aligned);
    LONG mask = static_cast<LONG>(0xFF) << shift;
    LONG old_word = *word;
    while (true) {
        LONG new_word = (old_word & ~mask) | (static_cast<LONG>(val) << shift);
        LONG prev     = InterlockedCompareExchange(word, new_word, old_word);
        if (prev == old_word) return;
        old_word = prev;
    }
}

int __cxa_guard_acquire(long long* guard) {
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

void __cxa_guard_release(long long* guard) {
    volatile BYTE* flag      = reinterpret_cast<volatile BYTE*>(guard);
    volatile BYTE* lock_byte = reinterpret_cast<volatile BYTE*>(guard) + 1;
    atomic_store_byte(flag, 0x1);
    atomic_store_byte(lock_byte, 0);
}

void __cxa_guard_abort(long long* guard) {
    volatile BYTE* lock_byte = reinterpret_cast<volatile BYTE*>(guard) + 1;
    atomic_store_byte(lock_byte, 0);
}

// ── MinGW Threading / Mutex / TLS ─────────────────────────────────────────────
#define RT_MAX_KEYS 256
static DWORD         rt_tls_slots[RT_MAX_KEYS];
static volatile long rt_tls_count = 0;
static volatile long rt_tls_lock  = 0;

int pthread_key_create(unsigned* key, void (*/*dtor*/)(void*)) {
    while (InterlockedCompareExchange(&rt_tls_lock, 1, 0) != 0) YieldProcessor();
    int k = static_cast<int>(rt_tls_count);
    if (k < RT_MAX_KEYS) {
        rt_tls_slots[k] = TlsAlloc();
        rt_tls_count++;
        *key = static_cast<unsigned>(k);
        InterlockedExchange(&rt_tls_lock, 0);
        return 0;
    }
    InterlockedExchange(&rt_tls_lock, 0);
    panic_abort("Too many TLS keys!");
    return 1;
}

void* pthread_getspecific(unsigned key) {
    if (key >= static_cast<unsigned>(rt_tls_count)) return nullptr;
    return TlsGetValue(rt_tls_slots[key]);
}

int pthread_setspecific(unsigned key, const void* val) {
    if (key >= static_cast<unsigned>(rt_tls_count)) return 1;
    return TlsSetValue(rt_tls_slots[key], const_cast<LPVOID>(val)) ? 0 : 1;
}

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
        panic_abort("pthread_mutex_destroy: invalid or destroyed mutex");
    DeleteCriticalSection(&m->cs); m->sentinel = 0; return 0;
}

unsigned long pthread_self(void) { return GetCurrentThreadId(); }
int pthread_once(long* control, void (*init)(void)) {
    if (InterlockedCompareExchange(control, 1, 0) == 0) init();
    return 0;
}

int   __gthrw_pthread_active_p(void)                            { return 1; }
int   __gthrw___pthread_key_create(void* k, void(*d)(void*))    { return pthread_key_create(reinterpret_cast<unsigned*>(k), d); }
void* __gthrw___pthread_getspecific(unsigned k)                 { return pthread_getspecific(k); }
int   __gthrw___pthread_setspecific(unsigned k, const void* v)  { return pthread_setspecific(k, v); }
int   __gthrw___pthread_mutex_lock(void* m)                     { return pthread_mutex_lock(reinterpret_cast<rt_mutex_t*>(m)); }
int   __gthrw___pthread_mutex_unlock(void* m)                   { return pthread_mutex_unlock(reinterpret_cast<rt_mutex_t*>(m)); }

// ── Environment & CRT Process Controls ────────────────────────────────────────
static char** g_environ_ptrs = nullptr;
char** _environ = nullptr;
char** environ  = nullptr;

char*** __cdecl __p__environ(void) {
    if (!g_environ_ptrs) {
        LPCH envBlock = GetEnvironmentStringsA();
        if (envBlock) {
            size_t count = 0;
            for (LPCH p = envBlock; *p != '\0'; p += lstrlenA(p) + 1) count++;
            g_environ_ptrs = static_cast<char**>(malloc((count + 1) * sizeof(char*)));
            if (g_environ_ptrs) {
                size_t idx = 0;
                for (LPCH p = envBlock; *p != '\0'; p += lstrlenA(p) + 1) {
                    g_environ_ptrs[idx++] = p;
                }
                g_environ_ptrs[idx] = nullptr;
            }
        }
        if (!g_environ_ptrs) {
            static char* empty_env[] = { nullptr };
            g_environ_ptrs = empty_env;
        }
        _environ = g_environ_ptrs;
        environ  = g_environ_ptrs;
    }
    return &g_environ_ptrs;
}

char* getenv(const char* name) {
    if (!name || !*name) return nullptr;

    __p__environ();
    if (!_environ) return nullptr;

    size_t name_len = strlen(name);

    for (char** env = _environ; *env != nullptr; ++env) {
        const char* p = *env;
        if (_strnicmp(p, name, name_len) == 0 && p[name_len] == '=') {
            return const_cast<char*>(p + name_len + 1);
        }
    }

    return nullptr;
}
decltype(&getenv) __imp_getenv = &getenv;

int __cdecl _configthreadlocale(int) { return 2; }
void __cdecl __setusermatherr(void*) {}

void __cdecl _cexit(void) { run_atexit_handlers(); }
void (__cdecl *__imp__cexit)(void) = &_cexit;

int __cdecl _configure_narrow_argv(int) { return 0; }
int (__cdecl *__imp__configure_narrow_argv)(int) = &_configure_narrow_argv;

int __cdecl _initialize_narrow_environment(void) { return 0; }
int (__cdecl *__imp__initialize_narrow_environment)(void) = &_initialize_narrow_environment;

int __cdecl _seh_filter_exe(unsigned long, struct _EXCEPTION_POINTERS*) { return 0; }
int (__cdecl *__imp__seh_filter_exe)(unsigned long, struct _EXCEPTION_POINTERS*) = &_seh_filter_exe;

typedef void (__cdecl * _PVFV)(void);
void __cdecl _initterm(_PVFV* pfbegin, _PVFV* pfend) {
    while (pfbegin < pfend) {
        if (*pfbegin) (*pfbegin)();
        pfbegin++;
    }
}
void (__cdecl *__imp__initterm)(_PVFV*, _PVFV*) = &_initterm;

typedef int (__cdecl * _PIFV)(void);
int __cdecl _initterm_e(_PIFV* pfbegin, _PIFV* pfend) {
    while (pfbegin < pfend) {
        if (*pfbegin) {
            int res = (*pfbegin)();
            if (res != 0) return res;
        }
        pfbegin++;
    }
    return 0;
}
int (__cdecl *__imp__initterm_e)(_PIFV*, _PIFV*) = &_initterm_e;

void __cdecl _set_app_type(int) {}
void (__cdecl *__imp__set_app_type)(int) = &_set_app_type;

typedef void (__cdecl * _invalid_parameter_handler)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);
_invalid_parameter_handler __cdecl _set_invalid_parameter_handler(_invalid_parameter_handler) { return nullptr; }
_invalid_parameter_handler (__cdecl *__imp__set_invalid_parameter_handler)(_invalid_parameter_handler) = &_set_invalid_parameter_handler;

__attribute__((noreturn)) void __cdecl abort(void) {
    panic_abort("abort() called");
    __builtin_unreachable();
}
void (__cdecl *__imp_abort)(void) = &abort;

__attribute__((noreturn)) void __cdecl exit(int code) {
    run_atexit_handlers();
    mem_report_leaks();
    ExitProcess(static_cast<UINT>(code));
    __builtin_unreachable();
}
void (__cdecl *__imp_exit)(int) = &exit;

__attribute__((noreturn)) void __cdecl _exit(int code) {
    ExitProcess(static_cast<UINT>(code));
    __builtin_unreachable();
}
void (__cdecl *__imp__exit)(int) = &_exit;

#ifndef _SIG_FN_T_DEFINED
#  define _SIG_FN_T_DEFINED
typedef void (__cdecl * _sig_fn_t)(int);
#endif
_sig_fn_t __cdecl signal(int, _sig_fn_t) { return nullptr; }
_sig_fn_t (__cdecl *__imp_signal)(int, _sig_fn_t) = &signal;

} // extern "C"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif