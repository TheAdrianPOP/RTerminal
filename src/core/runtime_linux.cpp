// src/engine/core/runtime_linux.cpp
// Linux counterpart to the Windows-only block in runtime.cpp.
// Provides runtime_init, panic_abort, mem_report_leaks, and operator new/delete.

#ifndef _WIN32

#include "internal.h"
#include <unistd.h>

// ── Signal handler ────────────────────────────────────────────────────────────

// write() is one of the few async-signal-safe functions.  We must use it
// instead of fprintf here — but GCC marks it [[nodiscard]].  Inside a signal
// handler there is nothing useful we can do if the write fails (the process
// is already dying), so we intentionally discard the return.  The void cast
// is the standard idiom to suppress -Wunused-result without disabling the
// warning globally.
#define SIGNAL_WRITE(fd, buf, len) do { auto _res = write((fd), (buf), (len)); (void)_res; } while(0)

static void linux_signal_handler(int sig) {
    const char* reason = "Unknown signal";
    switch (sig) {
        case SIGSEGV: reason = "Segmentation fault";       break;
        case SIGBUS:  reason = "Bus error";                break;
        case SIGFPE:  reason = "Floating point exception"; break;
        case SIGILL:  reason = "Illegal instruction";      break;
        case SIGABRT: reason = "Abort";                    break;
        case SIGPIPE: reason = "Broken pipe";              break;
    }
    static const char prefix[] = "\n[Fatal] ";
    SIGNAL_WRITE(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    SIGNAL_WRITE(STDERR_FILENO, reason, strlen(reason));
    SIGNAL_WRITE(STDERR_FILENO, "\n",   1);
    _exit(1);
}

#undef SIGNAL_WRITE

void runtime_init() {
    struct sigaction sa{};
    sa.sa_handler = linux_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;   // one-shot — avoids infinite recursion

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    // SIGPIPE: ignore so a closed terminal doesn't kill us mid-frame
    struct sigaction ign{};
    ign.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ign, nullptr);
}

// ── panic_abort ───────────────────────────────────────────────────────────────

extern "C" void panic_abort(const char* fmt, ...) {
    char message[512] = "Fatal Error: ";
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(message + 13, sizeof(message) - 13, fmt, args);
        va_end(args);
    } else {
        strncat(message, "Unknown engine error.", sizeof(message) - 14);
    }
    fprintf(stderr, "\n%s\n", message);
    fflush(stderr);
    abort();
}

// ── mem_report_leaks (debug stub) ─────────────────────────────────────────────
// On Linux we rely on Valgrind / ASan rather than a custom tracker.
// Provide the symbol so the TU compiles in all configs.

#ifdef _DEBUG
extern "C" void mem_report_leaks() {
    fprintf(stderr, "[MEMORY] Use valgrind or -fsanitize=address for leak detection.\n");
}
#endif

// ── operator new / delete ─────────────────────────────────────────────────────
// On Linux we don't use -nostdlib++, so the system libstdc++ provides these.
// We override them only to call panic_abort on failure, matching Windows behaviour.

void* operator new(size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) panic_abort("operator new failed for %zu bytes", n);
    return p;
}
void* operator new[](size_t n) { return ::operator new(n); }

void* operator new(size_t n, std::align_val_t al) {
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<size_t>(al), n ? n : 1) != 0)
        panic_abort("aligned operator new failed for %zu bytes (align %zu)", n, static_cast<size_t>(al));
    return p;
}
void* operator new[](size_t n, std::align_val_t al) { return ::operator new(n, al); }

void operator delete(void* p) noexcept                               { free(p); }
void operator delete[](void* p) noexcept                             { free(p); }
void operator delete(void* p, size_t) noexcept                       { free(p); }
void operator delete[](void* p, size_t) noexcept                     { free(p); }
void operator delete(void* p, std::align_val_t) noexcept             { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept           { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept     { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept   { free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept        { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept      { free(p); }

#endif // !_WIN32