#include "internal.h"

#ifdef _WIN32

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif

#ifdef _environ
#  undef _environ
#endif
#ifdef environ
#  undef environ
#endif

// Macro to cleanly define x86_64 / x86 __imp_ function pointers in read-only .rdata
#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
  #define EXPORT_IMP(fn) extern "C" __attribute__((used)) auto const __imp_##fn = &fn
#else
  #define EXPORT_IMP(fn) extern "C" __attribute__((used)) auto const __imp_##fn = &fn; \
                         extern "C" __attribute__((used)) auto const __imp__##fn = &fn
#endif

// ── Shared CRT State ──────────────────────────────────────────────────────────
int g_crt_errno = 0;

extern "C" {

__attribute__((noreturn)) void panic_abort(const char* fmt, ...);

int* __cdecl _errno(void) { return &g_crt_errno; }
EXPORT_IMP(_errno);

// ── Memory Byte Operations ───────────────────────────────────────────────────
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC push_options
#  pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

#define MEM_PRIMITIVE_ATTR __attribute__((used, externally_visible, no_builtin))

MEM_PRIMITIVE_ATTR void* memset(void* s, int c, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(s);
    unsigned char  val = static_cast<unsigned char>(c);
    while (n--) *p++ = val;
    return s;
}
EXPORT_IMP(memset);

MEM_PRIMITIVE_ATTR void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    while (n--) *d++ = *s++;
    return dest;
}
EXPORT_IMP(memcpy);

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
EXPORT_IMP(memmove);

MEM_PRIMITIVE_ATTR int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}
EXPORT_IMP(memcmp);

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
EXPORT_IMP(strcasecmp);

int strncasecmp(const char* s1, const char* s2, size_t n) { return _strnicmp(s1, s2, n); }
EXPORT_IMP(strncasecmp);

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
        g_crt_errno = 34; // ERANGE
        return neg ? LONG_MIN : LONG_MAX;
    }
    return neg ? -static_cast<long>(acc) : static_cast<long>(acc);
}

long strtol(const char* nptr, char** endptr, int base) { return strtol_impl(nptr, endptr, base); }
EXPORT_IMP(strtol);

long __mingw_strtol(const char* nptr, char** endptr, int base) { return strtol_impl(nptr, endptr, base); }
EXPORT_IMP(__mingw_strtol);

int atoi(const char* nptr) { return static_cast<int>(strtol_impl(nptr, nullptr, 10)); }
EXPORT_IMP(atoi);

long atol(const char* nptr) { return strtol_impl(nptr, nullptr, 10); }
EXPORT_IMP(atol);

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
EXPORT_IMP(atexit);

int __cdecl _crt_atexit(void (*func)(void)) { return atexit(func); }
EXPORT_IMP(_crt_atexit);

#ifndef _ONEXIT_T_DEFINED
#  define _ONEXIT_T_DEFINED
typedef int (__cdecl * _onexit_t)(void);
#endif

_onexit_t __cdecl _onexit(_onexit_t func) {
    if (atexit(reinterpret_cast<void (*)(void)>(func)) == 0) return func;
    return nullptr;
}
EXPORT_IMP(_onexit);

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

// ── Environment Helpers ───────────────────────────────────────────────────────
static char** g_environ_ptrs = nullptr;
char** _environ = nullptr;
char** environ  = nullptr;

char*** __cdecl __p__environ(void) {
    if (!g_environ_ptrs) {
        LPCH envBlock = GetEnvironmentStringsA();
        if (envBlock) {
            size_t count = 0;
            for (LPCH p = envBlock; *p != '\0'; p += lstrlenA(p) + 1) count++;

            g_environ_ptrs = static_cast<char**>(
                HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(char*))
            );

            if (g_environ_ptrs) {
                size_t idx = 0;
                for (LPCH p = envBlock; *p != '\0'; p += lstrlenA(p) + 1) {
                    g_environ_ptrs[idx++] = p;
                }
                g_environ_ptrs[idx] = nullptr;
            }
        }

        if (!g_environ_ptrs) {
            static char* const empty_env[] = { nullptr };
            g_environ_ptrs = const_cast<char**>(empty_env);
        }

        _environ = g_environ_ptrs;
        environ  = g_environ_ptrs;
    }
    return &g_environ_ptrs;
}

char* getenv(const char* name) {
    if (!name || !*name) return nullptr;

    static char* s_env_buf = nullptr;
    if (!s_env_buf) {
        s_env_buf = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, 2048));
        if (!s_env_buf) return nullptr;
    }

    DWORD len = GetEnvironmentVariableA(name, s_env_buf, 2048);
    if (len == 0 || len >= 2048) return nullptr;
    return s_env_buf;
}

// ── CRT Startup / Locale & Math Shims ─────────────────────────────────────────
int __cdecl _configthreadlocale(int) { return 2; }
void __cdecl __setusermatherr(void*) {}

// ── CRT Runtime Process Control Shims ─────────────────────────────────────────
static int    g_crt_argc = 1;
static char*  g_crt_dummy_argv[] = { (char*)"app", nullptr };
static char** g_crt_argv = g_crt_dummy_argv;

int* __cdecl __p___argc(void) { return &g_crt_argc; }
EXPORT_IMP(__p___argc);

char*** __cdecl __p___argv(void) { return &g_crt_argv; }
EXPORT_IMP(__p___argv);

void __cdecl _cexit(void) { run_atexit_handlers(); }
EXPORT_IMP(_cexit);

int __cdecl _configure_narrow_argv(int) { return 0; }
EXPORT_IMP(_configure_narrow_argv);

int __cdecl _initialize_narrow_environment(void) { return 0; }
EXPORT_IMP(_initialize_narrow_environment);

int __cdecl _seh_filter_exe(unsigned long, struct _EXCEPTION_POINTERS*) { return 0; }
EXPORT_IMP(_seh_filter_exe);

typedef void (__cdecl * _PVFV)(void);
void __cdecl _initterm(_PVFV* pfbegin, _PVFV* pfend) {
    while (pfbegin < pfend) {
        if (*pfbegin) (*pfbegin)();
        pfbegin++;
    }
}
EXPORT_IMP(_initterm);

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
EXPORT_IMP(_initterm_e);

void __cdecl _set_app_type(int) {}
EXPORT_IMP(_set_app_type);

typedef void (__cdecl * _invalid_parameter_handler)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);
_invalid_parameter_handler __cdecl _set_invalid_parameter_handler(_invalid_parameter_handler) { return nullptr; }
EXPORT_IMP(_set_invalid_parameter_handler);

__attribute__((noreturn)) void __cdecl abort(void) {
    panic_abort("abort() called");
    __builtin_unreachable();
}
EXPORT_IMP(abort);

__attribute__((noreturn)) void __cdecl exit(int code) {
    run_atexit_handlers();
    ExitProcess(static_cast<UINT>(code));
    __builtin_unreachable();
}
EXPORT_IMP(exit);

__attribute__((noreturn)) void __cdecl _exit(int code) {
    ExitProcess(static_cast<UINT>(code));
    __builtin_unreachable();
}
EXPORT_IMP(_exit);

#ifndef _SIG_FN_T_DEFINED
#  define _SIG_FN_T_DEFINED
typedef void (__cdecl * _sig_fn_t)(int);
#endif
_sig_fn_t __cdecl signal(int, _sig_fn_t) { return nullptr; }
EXPORT_IMP(signal);

} // extern "C"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#endif // _WIN32