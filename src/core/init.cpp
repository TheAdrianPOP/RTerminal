// init.cpp — Engine Initialization, Exception Handling & Crash Diagnostics
// Dependencies: kernel32.dll only

#ifndef _WIN32
#  error "init.cpp is only intended for Windows targets."
#endif

#include "internal.h"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#endif

// ── Global Instance Handle ───────────────────────────────────────────────────
HINSTANCE g_hInstance = NULL;

extern "C" {

// ── External Runtime Linkage ─────────────────────────────────────────────────
int vsnprintf(char* buf, size_t n, const char* fmt, va_list args);
int snprintf(char* buf, size_t n, const char* fmt, ...);

// ── Crash Diagnostics Storage ────────────────────────────────────────────────
static const EXCEPTION_POINTERS* g_active_exception = nullptr;
static volatile LONG             s_crash_lock = 0;
static DWORD                     s_crashing_thread_id = 0;

// ── Single Unified Box Renderer ──────────────────────────────────────────────
__attribute__((noinline))
static void box_row(HANDLE h, const char* text, char border = '|', char fill = ' ') {
    char buf[76];
    const char* p = text ? text : "";

    do {
        buf[0] = border;
        memset(buf + 1, fill, 70);
        buf[71] = border;
        buf[72] = '\r';
        buf[73] = '\n';

        int len = 0;
        while (p[len] && p[len] != '\r' && p[len] != '\n' && len < 70) len++;
        memcpy(buf + 1, p, len);

        DWORD written;
        WriteFile(h, buf, 74, &written, NULL);

        p += len;
        while (*p == '\r' || *p == '\n') p++;
    } while (*p);
}

// ── Frame Extraction Helper ───────────────────────────────────────────────────
__attribute__((noinline))
static unsigned short capture_frames(void** frames) {
    unsigned short n = 0;
    if (g_active_exception && g_active_exception->ContextRecord) {
        auto* ctx = g_active_exception->ContextRecord;
        if (ctx->Rip) frames[n++] = reinterpret_cast<void*>(ctx->Rip);

        auto** sp = reinterpret_cast<void**>(ctx->Rsp);
        for (int i = 0; sp && i < 16 && n < 8; ++i) {
            uintptr_t a = reinterpret_cast<uintptr_t>(sp[i]);
            if ((a >> 16) > 0 && (a >> 47) == 0) {
                frames[n++] = reinterpret_cast<void*>(a);
            }
        }
    }
    if (!n) {
        n = static_cast<unsigned short>(CaptureStackBackTrace(1, 8, frames, NULL));
    }
    return n;
}

// ── Fatal Crash & Abort Handler ──────────────────────────────────────────────
__attribute__((noreturn)) void panic_abort(const char* fmt, ...) {
    char panic_message[160];
    char crash_buf[512];
    void* stack_frames[8];

    va_list args;
    va_start(args, fmt);
    vsnprintf(panic_message, sizeof(panic_message), fmt ? fmt : "Unknown engine error", args);
    va_end(args);

    unsigned short frames = capture_frames(stack_frames);

    DWORD pid    = GetCurrentProcessId();
    DWORD tid    = GetCurrentThreadId();
    HMODULE hMod = GetModuleHandleA(NULL);
    void* fault  = (g_active_exception && g_active_exception->ExceptionRecord)
                   ? g_active_exception->ExceptionRecord->ExceptionAddress : NULL;

    // 1. Format the entire crash report in 3 compact steps
    int len = snprintf(crash_buf, sizeof(crash_buf),
        "\n  [CRASH REASON]\n    %s\n\n"
        "  [DIAGNOSTICS]\n"
        "    PID: %lu | TID: %lu | Base: 0x%p\n"
        "    Fault: 0x%p\n\n"
        "  [STACK BACKTRACE]\n",
        panic_message, pid, tid, hMod, fault);

    for (unsigned short i = 0; i < frames; ++i) {
        len += snprintf(crash_buf + len, sizeof(crash_buf) - len,
            "    [%02u] 0x%p\n", i, stack_frames[i]);
    }

    len += snprintf(crash_buf + len, sizeof(crash_buf) - len,
        "\n  [LOG FILE]\n    Saved to 'crash_log.txt'\n");

    // 2. Write log file in one single syscall
    HANDLE hFile = CreateFileA("crash_log.txt", GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, crash_buf + 1, len - 1, &written, NULL); // +1 skips leading '\n'
        CloseHandle(hFile);
    }

    // 3. Render crash card to console
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if ((!hErr || hErr == INVALID_HANDLE_VALUE) &&
        (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())) {
        hErr = GetStdHandle(STD_ERROR_HANDLE);
    }

    if (hErr && hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        bool vt = GetConsoleMode(hErr, &mode) && SetConsoleMode(hErr, mode | 0x0004);

        SetConsoleTextAttribute(hErr, 0x4F); // BACKGROUND_RED | FOREGROUND_WHITE_INTENSE

        DWORD w;
        WriteFile(hErr, vt ? "\x1b[97;41m\r\n" : "\r\n", vt ? 9 : 2, &w, NULL);

        box_row(hErr, nullptr, '+', '=');
        box_row(hErr, "                         !!! ENGINE PANIC !!!");
        box_row(hErr, nullptr, '+', '-');
        box_row(hErr, crash_buf);
        box_row(hErr, nullptr, '+', '-');
        box_row(hErr, "  Press Enter to exit...");
        box_row(hErr, nullptr, '+', '=');

        WriteFile(hErr, vt ? "\x1b[0m\r\n\r\n" : "\r\n\r\n", vt ? 8 : 4, &w, NULL);
    }

    // Wait for Enter key
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD m, r;
    char dummy;
    if (GetConsoleMode(hIn, &m)) {
        FlushConsoleInputBuffer(hIn);
        ReadFile(hIn, &dummy, 1, &r, NULL);
    }

    SetConsoleTextAttribute(hErr, 0x07);
    TerminateProcess(GetCurrentProcess(), 0xDEADBEEF);
    __builtin_unreachable();
}

// ── Exception Filter ─────────────────────────────────────────────────────────
static LONG WINAPI engine_exception_filter(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord || IsDebuggerPresent()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Only intercept fatal hardware/NTSTATUS errors (0xC0000000+).
    // This automatically filters out debugger notifications, C++ exceptions, and RPC codes.
    if ((code & 0xF0000000) != 0xC0000000) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Prevent double-fault recursion
    DWORD current_tid = GetCurrentThreadId();
    if (InterlockedCompareExchange(&s_crash_lock, 1, 0) != 0) {
        if (s_crashing_thread_id == current_tid) {
            TerminateProcess(GetCurrentProcess(), 0xDEAD0001);
        }
        for (;;) Sleep(1000);
    }
    s_crashing_thread_id = current_tid;

    const char* reason = "Hardware Fault";
    if (code >= EXCEPTION_FLT_DENORMAL_OPERAND && code <= EXCEPTION_FLT_UNDERFLOW) {
        reason = "Floating Point Fault";
    } else {
        struct FaultDesc { DWORD code; const char* name; };
        static const FaultDesc kFaults[] = {
            { EXCEPTION_ACCESS_VIOLATION,         "Access Violation"          },
            { EXCEPTION_IN_PAGE_ERROR,            "In-page Error"             },
            { EXCEPTION_ILLEGAL_INSTRUCTION,      "Illegal Instruction"       },
            { EXCEPTION_NONCONTINUABLE_EXCEPTION, "Non-continuable Exception" },
            { EXCEPTION_INVALID_DISPOSITION,      "Invalid Disposition"       },
            { EXCEPTION_ARRAY_BOUNDS_EXCEEDED,    "Array Bounds Exceeded"     },
            { EXCEPTION_INT_DIVIDE_BY_ZERO,       "Integer Divide by Zero"    },
            { EXCEPTION_INT_OVERFLOW,             "Integer Overflow"          },
            { EXCEPTION_PRIV_INSTRUCTION,         "Privileged Instruction"    },
            { EXCEPTION_STACK_OVERFLOW,           "Stack Overflow"            },
            { EXCEPTION_DATATYPE_MISALIGNMENT,    "Datatype Misalignment"     },
        };
        for (const auto& f : kFaults) {
            if (f.code == code) { reason = f.name; break; }
        }
    }

    g_active_exception = ep;

    panic_abort("Hardware Crash: %s (0x%08lX) at 0x%p",
                reason, static_cast<unsigned long>(code),
                ep->ExceptionRecord->ExceptionAddress);

    return EXCEPTION_CONTINUE_SEARCH;
}

// ── Dynamic user32 Function Pointers ─────────────────────────────────────────
#define DEF_U32_IMP(name) \
    __attribute__((used, externally_visible, visibility("default"))) \
    void* __imp_##name = nullptr

    DEF_U32_IMP(GetCursorPos);
    DEF_U32_IMP(SetCursorPos);
    DEF_U32_IMP(ShowCursor);
    DEF_U32_IMP(GetForegroundWindow);
    DEF_U32_IMP(GetWindowRect);
    DEF_U32_IMP(GetClassNameA);
    DEF_U32_IMP(GetAsyncKeyState);
    DEF_U32_IMP(GetRawInputDeviceList);
    DEF_U32_IMP(CreateWindowExA);
    DEF_U32_IMP(DestroyWindow);
    DEF_U32_IMP(DefWindowProcA);
    DEF_U32_IMP(RegisterClassA);
    DEF_U32_IMP(GetDC);
    DEF_U32_IMP(ReleaseDC);

#undef DEF_U32_IMP

// Dynamically bind user32 APIs via a compact table loop
void init_user32() {
    HMODULE u32 = LoadLibraryA("user32.dll");
    if (!u32) return;

    static const char kU32Names[] =
        "GetCursorPos\0"
        "SetCursorPos\0"
        "ShowCursor\0"
        "GetForegroundWindow\0"
        "GetWindowRect\0"
        "GetClassNameA\0"
        "GetAsyncKeyState\0"
        "GetRawInputDeviceList\0"
        "CreateWindowExA\0"
        "DestroyWindow\0"
        "DefWindowProcA\0"
        "RegisterClassA\0"
        "GetDC\0"
        "ReleaseDC\0";

    void** const kPtrs[] = {
        &__imp_GetCursorPos, &__imp_SetCursorPos, &__imp_ShowCursor,
        &__imp_GetForegroundWindow, &__imp_GetWindowRect, &__imp_GetClassNameA,
        &__imp_GetAsyncKeyState, &__imp_GetRawInputDeviceList, &__imp_CreateWindowExA,
        &__imp_DestroyWindow, &__imp_DefWindowProcA, &__imp_RegisterClassA,
        &__imp_GetDC, &__imp_ReleaseDC
    };

    const char* p = kU32Names;
    for (void** ptr : kPtrs) {
        *ptr = (void*)GetProcAddress(u32, p);
        while (*p++) {}
    }
}

// ── Public Runtime Initialization ───────────────────────────────────────────
void runtime_init(void) {
    g_hInstance = GetModuleHandleA(NULL);

    init_user32();

    ULONG stackGuarantee = 32 * 1024;
    SetThreadStackGuarantee(&stackGuarantee);

    AddVectoredExceptionHandler(0, engine_exception_filter);
    SetUnhandledExceptionFilter(engine_exception_filter);
}

// ── MinGW Constructor Caller Stub ───────────────────────────────────────────
__attribute__((used, externally_visible))
void __main(void) {}

} // extern "C"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif