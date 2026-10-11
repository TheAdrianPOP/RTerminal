#ifdef _WIN32

// ── Standalone Type Definitions (Zero Includes) ──────────────────────────────

typedef __SIZE_TYPE__      size_t;
typedef __UINTPTR_TYPE__   uintptr_t;

typedef void*              HANDLE;
typedef char*              LPSTR;
typedef unsigned int       UINT;
typedef void (*pvfn)(void);

#if defined(_M_IX86) || defined(__i386__)
  #define WINAPI           __attribute__((stdcall))
#else
  #define WINAPI
#endif

// Direct import calling convention to prevent linker thunks in .text
#define WINAPI_IMPORT      __declspec(dllimport) WINAPI

// ── Win32 API Declarations ───────────────────────────────────────────────────

extern "C" {
    WINAPI_IMPORT LPSTR   GetCommandLineA(void);
    WINAPI_IMPORT HANDLE  GetProcessHeap(void);
    WINAPI_IMPORT void*   HeapAlloc(HANDLE hHeap, UINT dwFlags, size_t dwBytes);
    __attribute__((noreturn)) WINAPI_IMPORT void ExitProcess(UINT uExitCode);

    // Optional user runtime hook
    __attribute__((weak)) void runtime_init(void);
}

// ── Windows PE Section Markers & GCC Tables ───────────────────────────────────

extern "C" {
    __attribute__((section(".CRT$XCA"), used)) const pvfn __xc_a = nullptr;
    __attribute__((section(".CRT$XCZ"), used)) const pvfn __xc_z = nullptr;

    __attribute__((section(".CRT$XTA"), used)) const pvfn __xt_a = nullptr;
    __attribute__((section(".CRT$XTZ"), used)) const pvfn __xt_z = nullptr;

    // Weak declarations: resolved by the GNU ld linker script without creating
    // duplicate symbols in .data or pulling in libgcc.a
    extern pvfn __CTOR_LIST__[] __attribute__((weak));
    extern pvfn __DTOR_LIST__[] __attribute__((weak));
}

extern "C" {

__attribute__((used, externally_visible))
float __powisf2(float a, int b) {
    float r = 1.0f;
    unsigned int n = (b < 0) ? -b : b;
    float x = a;
    while (n) {
        if (n & 1) r *= x;
        x *= x;
        n >>= 1;
    }
    return (b < 0) ? (1.0f / r) : r;
}

} // extern "C"

// ── Constructors & Destructors ───────────────────────────────────────────────

static void run_init_array(const pvfn* start, const pvfn* end) {
    if (start < end) {
        for (const pvfn* cur = start + 1; cur < end; ++cur) {
            pvfn func = *cur;
            if (func && func != reinterpret_cast<pvfn>(static_cast<uintptr_t>(-1))) {
                func();
            }
        }
    }
}

static void run_global_constructors(void) {
    // Modern MinGW-w64 on x64 puts initializers in .CRT$XCU (between XCA and XCZ)
    run_init_array(&__xc_a, &__xc_z);

    // Fallback if legacy .ctors table is populated
    if (&__CTOR_LIST__ != nullptr) {
        uintptr_t header = reinterpret_cast<uintptr_t>(__CTOR_LIST__[0]);
        if (header == static_cast<uintptr_t>(-1)) {
            for (size_t i = 1; __CTOR_LIST__[i] != nullptr && __CTOR_LIST__[i] != reinterpret_cast<pvfn>(static_cast<uintptr_t>(-1)); ++i) {
                __CTOR_LIST__[i]();
            }
        } else if (header > 0 && header < 0xFFFF) {
            for (size_t i = header; i >= 1; --i) {
                if (__CTOR_LIST__[i]) __CTOR_LIST__[i]();
            }
        }
    }
}

static void run_global_destructors(void) {
    // Fallback if legacy .dtors table is populated
    if (&__DTOR_LIST__ != nullptr) {
        uintptr_t header = reinterpret_cast<uintptr_t>(__DTOR_LIST__[0]);
        if (header == static_cast<uintptr_t>(-1)) {
            for (size_t i = 1; __DTOR_LIST__[i] != nullptr && __DTOR_LIST__[i] != reinterpret_cast<pvfn>(static_cast<uintptr_t>(-1)); ++i) {
                __DTOR_LIST__[i]();
            }
        } else if (header > 0 && header < 0xFFFF) {
            for (size_t i = 1; i <= header; ++i) {
                if (__DTOR_LIST__[i]) __DTOR_LIST__[i]();
            }
        }
    }

    run_init_array(&__xt_a, &__xt_z);
}

// ── Dynamic Command-Line Parsing ─────────────────────────────────────────────

static void parse_cmdline(int* out_argc, char*** out_argv) {
    const char* cmd = GetCommandLineA();
    if (!cmd) cmd = "";

    size_t len = 0;
    while (cmd[len]) len++;

    HANDLE heap = GetProcessHeap();
    size_t max_args = (len / 2) + 2;

    // Single contiguous allocation: pointer array followed by parsed string storage
    size_t total_bytes = (max_args * sizeof(char*)) + (len + 1);
    char** argv = reinterpret_cast<char**>(HeapAlloc(heap, 0, total_bytes));

    if (!argv) {
        *out_argc = 0;
        *out_argv = nullptr;
        return;
    }

    char* dst = reinterpret_cast<char*>(argv + max_args);
    const char* src = cmd;
    int argc = 0;

    while (*src) {
        while (*src == ' ' || *src == '\t') src++;
        if (!*src) break;

        argv[argc++] = dst;
        bool in_quotes = false;

        while (*src) {
            if (*src == '\\') {
                size_t num_bs = 0;
                while (*src == '\\') {
                    num_bs++;
                    src++;
                }

                if (*src == '"') {
                    for (size_t k = 0; k < num_bs / 2; ++k) *dst++ = '\\';
                    if (num_bs % 2 != 0) {
                        *dst++ = '"';
                        src++;
                    } else {
                        in_quotes = !in_quotes;
                        src++;
                    }
                } else {
                    for (size_t k = 0; k < num_bs; ++k) *dst++ = '\\';
                }
            } else if (*src == '"') {
                in_quotes = !in_quotes;
                src++;
            } else if (!in_quotes && (*src == ' ' || *src == '\t')) {
                src++;
                break;
            } else {
                *dst++ = *src++;
            }
        }
        *dst++ = '\0';
    }

    if (argc == 0) {
        argv[0] = dst;
        *dst++ = '\0';
        argc = 1;
    }
    argv[argc] = nullptr;

    *out_argc = argc;
    *out_argv = argv;
}

// ── Entry Points ─────────────────────────────────────────────────────────────

extern "C" int main(int argc, char** argv);

extern "C" __attribute__((noreturn)) void __cdecl mainCRTStartup(void) {
    if (runtime_init) runtime_init();
    run_global_constructors();

    int argc = 0;
    char** argv = nullptr;
    parse_cmdline(&argc, &argv);

    int res = main(argc, argv);

    run_global_destructors();
    ExitProcess(static_cast<UINT>(res));
}

// Forward GUI entry point directly to mainCRTStartup
extern "C" __attribute__((noreturn)) void __cdecl WinMainCRTStartup(void) {
    mainCRTStartup();
}

#endif // _WIN32