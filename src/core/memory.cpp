#include "internal.h"

#ifndef SIZE_MAX
#  define SIZE_MAX (~((size_t)0))
#endif

// Macro to cleanly define x86_64 / x86 __imp_ function pointers in read-only .rdata
#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
  #define EXPORT_IMP(fn) extern "C" __attribute__((used)) auto const __imp_##fn = &fn
#else
  #define EXPORT_IMP(fn) extern "C" __attribute__((used)) auto const __imp_##fn = &fn; \
                         extern "C" __attribute__((used)) auto const __imp__##fn = &fn
#endif

// Only capture stack traces in Debug or when explicitly requested
#if defined(_WIN32) && (defined(DEBUG) || defined(_DEBUG) || defined(MEM_ENABLE_STACK_TRACE) || !defined(NDEBUG))
#  define MEM_CAPTURE_STACK 1
#  define STACK_FRAMES_MAX  12
#else
#  define MEM_CAPTURE_STACK 0
#  define STACK_FRAMES_MAX  0
#endif

#define ALLOC_MAGIC_ACTIVE 0xDEADBEEFu
#define ALLOC_MAGIC_FREED  0xBAADF00Du

struct AllocHeader {
    uint32_t     magic;
    size_t       size;
    void*        raw_block;
    uint64_t     alloc_id; // Monotonic identifier for snapshot diffing
    AllocHeader* prev;
    AllocHeader* next;
#if MEM_CAPTURE_STACK
    void*          stack[STACK_FRAMES_MAX];
    unsigned short stack_frames;
#endif
};

static AllocHeader*  g_alloc_head         = nullptr;
static volatile long g_mem_lock           = 0;
static size_t        g_total_allocated    = 0;
static size_t        g_active_alloc_count = 0;
static uint64_t      g_alloc_counter      = 0;
static float         g_bg_timer           = 0.0f;

static inline void mem_lock_acquire() {
#ifdef _WIN32
    while (InterlockedCompareExchange(&g_mem_lock, 1, 0) != 0) {
        YieldProcessor();
    }
#endif
}

static inline void mem_lock_release() {
#ifdef _WIN32
    InterlockedExchange(&g_mem_lock, 0);
#endif
}

static inline size_t round_up_pow2(size_t x) {
    if (x <= 1) return 1;
    x--;
    for (size_t i = 1; i < sizeof(size_t) * 8; i *= 2) {
        x |= x >> i;
    }
    return x + 1;
}

// ── Zero-CRT String Builder (No snprintf) ──────────────────────────────────────
struct RawOutBuf {
    char   data[512];
    size_t len = 0;

    void clear() {
        len = 0;
        data[0] = '\0';
    }

    void append_char(char c) {
        if (len + 1 < sizeof(data)) {
            data[len++] = c;
            data[len] = '\0';
        }
    }

    void append(const char* s) {
        if (!s) return;
        while (*s && (len + 1 < sizeof(data))) {
            data[len++] = *s++;
        }
        data[len] = '\0';
    }

    void append_uint(uint64_t val) {
        if (val == 0) {
            append_char('0');
            return;
        }
        char temp[24];
        int idx = 0;
        while (val > 0) {
            temp[idx++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
        while (idx > 0) {
            append_char(temp[--idx]);
        }
    }

    void append_uint_padded(uint64_t val, int width, char pad = '0') {
        char temp[24];
        int idx = 0;
        if (val == 0) {
            temp[idx++] = '0';
        } else {
            while (val > 0) {
                temp[idx++] = static_cast<char>('0' + (val % 10));
                val /= 10;
            }
        }
        while (idx < width) {
            temp[idx++] = pad;
        }
        while (idx > 0) {
            append_char(temp[--idx]);
        }
    }

    void append_hex_ptr(const void* ptr) {
        uintptr_t v = reinterpret_cast<uintptr_t>(ptr);
        append("0x");
        int num_hex_digits = static_cast<int>(sizeof(void*) * 2);
        for (int i = num_hex_digits - 1; i >= 0; --i) {
            uint8_t nibble = static_cast<uint8_t>((v >> (i * 4)) & 0xF);
            char c = (nibble < 10) ? static_cast<char>('0' + nibble) 
                                   : static_cast<char>('A' + (nibble - 10));
            append_char(c);
        }
    }
};

// ── Core Tracked Allocator ───────────────────────────────────────────────────
static void* raw_heap_alloc_tracked(size_t n, size_t alignment) {
    size_t req_bytes = n ? n : 1;
    if (alignment < alignof(AllocHeader)) {
        alignment = alignof(AllocHeader);
    }
    alignment = round_up_pow2(alignment);

    size_t header_size = sizeof(AllocHeader);
    if (req_bytes > (SIZE_MAX - header_size - alignment)) {
        panic_abort("Integer overflow in allocation");
        return nullptr;
    }

    size_t total_size = req_bytes + header_size + alignment;

#ifdef _WIN32
    void* raw = HeapAlloc(GetProcessHeap(), 0, total_size);
#else
    void* raw = ::malloc(total_size);
#endif

    if (!raw) {
        panic_abort("Out of memory in allocation");
        return nullptr;
    }

    uintptr_t raw_addr     = reinterpret_cast<uintptr_t>(raw) + header_size;
    uintptr_t aligned_addr = (raw_addr + (alignment - 1)) & ~(static_cast<uintptr_t>(alignment) - 1);

    AllocHeader* header = reinterpret_cast<AllocHeader*>(aligned_addr - sizeof(AllocHeader));
    header->magic       = ALLOC_MAGIC_ACTIVE;
    header->size        = req_bytes;
    header->raw_block   = raw;

#if MEM_CAPTURE_STACK
    header->stack_frames = CaptureStackBackTrace(2, STACK_FRAMES_MAX, header->stack, NULL);
#endif

    mem_lock_acquire();
    header->alloc_id = ++g_alloc_counter;
    header->prev     = nullptr;
    header->next     = g_alloc_head;
    if (g_alloc_head) {
        g_alloc_head->prev = header;
    }
    g_alloc_head = header;
    g_total_allocated += req_bytes;
    g_active_alloc_count++;
    mem_lock_release();

    return reinterpret_cast<void*>(aligned_addr);
}

static void raw_heap_free_tracked(void* p) {
    if (!p) return;

    AllocHeader* header = reinterpret_cast<AllocHeader*>(reinterpret_cast<uintptr_t>(p) - sizeof(AllocHeader));
    if (header->magic != ALLOC_MAGIC_ACTIVE) {
        return;
    }

    mem_lock_acquire();
    if (header->prev) header->prev->next = header->next;
    if (header->next) header->next->prev = header->prev;
    if (g_alloc_head == header) g_alloc_head = header->next;
    g_total_allocated -= header->size;
    g_active_alloc_count--;
    mem_lock_release();

    void* raw_block = header->raw_block;
    header->magic   = ALLOC_MAGIC_FREED;

#ifdef _WIN32
    HeapFree(GetProcessHeap(), 0, raw_block);
#else
    ::free(raw_block);
#endif
}

// ── DbgHelp Type Definitions ──────────────────────────────────────────────────
#if defined(_WIN32) && MEM_CAPTURE_STACK
typedef struct _IMAGEHLP_LINE64 {
    DWORD   SizeOfStruct;
    PVOID   Key;
    DWORD   LineNumber;
    PCHAR   FileName;
    DWORD64 Address;
} IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;

typedef struct _SYMBOL_INFO {
    ULONG   SizeOfStruct;
    ULONG   TypeIndex;
    ULONG64 Reserved[2];
    ULONG   Index;
    ULONG   Size;
    ULONG64 ModBase;
    ULONG   Flags;
    ULONG64 Value;
    ULONG64 Address;
    ULONG   Register;
    ULONG   Scope;
    ULONG   Tag;
    ULONG   NameLen;
    ULONG   MaxNameLen;
    CHAR    Name[1];
} SYMBOL_INFO, *PSYMBOL_INFO;

typedef BOOL  (WINAPI *pfn_SymInitialize)(HANDLE, PCSTR, BOOL);
typedef BOOL  (WINAPI *pfn_SymCleanup)(HANDLE);
typedef DWORD (WINAPI *pfn_SymSetOptions)(DWORD);
typedef BOOL  (WINAPI *pfn_SymGetLineFromAddr64)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
typedef BOOL  (WINAPI *pfn_SymFromAddr)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);

#define SYMOPT_LOAD_LINES        0x00000010
#define SYMOPT_UNDNAME           0x00000002
#define SYMOPT_DEFERRED_LOADS    0x00000004
#endif

// ── Live Memory Operations ────────────────────────────────────────────────────
extern "C" {

void mem_get_stats(size_t* out_bytes, size_t* out_count) {
    mem_lock_acquire();
    if (out_bytes) *out_bytes = g_total_allocated;
    if (out_count) *out_count = g_active_alloc_count;
    mem_lock_release();
}

uint64_t mem_snapshot_create() {
    mem_lock_acquire();
    uint64_t id = g_alloc_counter;
    mem_lock_release();
    return id;
}

void mem_dump_leaks_to_file(const char* filepath, uint64_t since_alloc_id) {
    if (!filepath) filepath = "memory_live.txt";

#ifdef _WIN32
    HANDLE hFile = CreateFileA(filepath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    RawOutBuf out;
    DWORD written = 0;

    auto emit = [&](const RawOutBuf& b) {
        if (b.len > 0) {
            WriteFile(hFile, b.data, static_cast<DWORD>(b.len), &written, NULL);
        }
    };

    mem_lock_acquire();

    out.clear();
    out.append("======================================================================\r\n");
    out.append(" LIVE MEMORY REPORT (Background Snapshot)\r\n");
    if (since_alloc_id > 0) {
        out.append(" Filter: Only showing allocations created after ID #");
        out.append_uint(since_alloc_id);
        out.append("\r\n");
    }
    out.append("======================================================================\r\n\r\n");
    emit(out);

    size_t leak_idx = 1;
    size_t reported_bytes = 0;
    size_t reported_count = 0;

#if MEM_CAPTURE_STACK
    HANDLE hProcess = GetCurrentProcess();
    HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
    
    pfn_SymInitialize        pSymInitialize        = nullptr;
    pfn_SymCleanup           pSymCleanup           = nullptr;
    pfn_SymSetOptions        pSymSetOptions        = nullptr;
    pfn_SymGetLineFromAddr64 pSymGetLineFromAddr64 = nullptr;
    pfn_SymFromAddr          pSymFromAddr          = nullptr;
    bool sym_ready = false;

    if (hDbgHelp) {
        pSymInitialize        = (pfn_SymInitialize)GetProcAddress(hDbgHelp, "SymInitialize");
        pSymCleanup           = (pfn_SymCleanup)GetProcAddress(hDbgHelp, "SymCleanup");
        pSymSetOptions        = (pfn_SymSetOptions)GetProcAddress(hDbgHelp, "SymSetOptions");
        pSymGetLineFromAddr64 = (pfn_SymGetLineFromAddr64)GetProcAddress(hDbgHelp, "SymGetLineFromAddr64");
        pSymFromAddr          = (pfn_SymFromAddr)GetProcAddress(hDbgHelp, "SymFromAddr");

        if (pSymInitialize && pSymSetOptions && pSymGetLineFromAddr64 && pSymFromAddr) {
            pSymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            if (pSymInitialize(hProcess, NULL, TRUE)) {
                sym_ready = true;
            }
        }
    }

    char symBuffer[sizeof(SYMBOL_INFO) + 256];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symBuffer;
#endif

    AllocHeader* curr = g_alloc_head;
    while (curr) {
        if (curr->alloc_id > since_alloc_id) {
            reported_bytes += curr->size;
            reported_count++;

            out.clear();
            out.append("[#");
            out.append_uint_padded(leak_idx++, 3, '0');
            out.append(" | ID:");
            out.append_uint(curr->alloc_id);
            out.append("] ");
            out.append_uint(curr->size);
            out.append(" bytes\r\n");
            emit(out);

#if MEM_CAPTURE_STACK
            for (unsigned short f = 0; f < curr->stack_frames; ++f) {
                out.clear();
                out.append("    [");
                out.append_uint_padded(f, 2, '0');
                out.append("] ");

                DWORD64 addr = (DWORD64)(uintptr_t)curr->stack[f];
                IMAGEHLP_LINE64 lineInfo = {};
                lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                DWORD displacement = 0;

                pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                pSymbol->MaxNameLen = 255;
                DWORD64 symDisplacement = 0;

                if (sym_ready && pSymGetLineFromAddr64(hProcess, addr, &displacement, &lineInfo)) {
                    out.append(lineInfo.FileName);
                    out.append(":");
                    out.append_uint(lineInfo.LineNumber);
                    if (pSymFromAddr(hProcess, addr, &symDisplacement, pSymbol)) {
                        out.append(" (");
                        out.append(pSymbol->Name);
                        out.append(")");
                    }
                } else {
                    out.append_hex_ptr(curr->stack[f]);
                }
                out.append("\r\n");
                emit(out);
            }
#endif
        }
        curr = curr->next;
    }

    out.clear();
    out.append("\r\n----------------------------------------------------------------------\r\n");
    out.append(" Summary: ");
    out.append_uint(reported_bytes);
    out.append(" bytes in ");
    out.append_uint(reported_count);
    out.append(" active allocation(s)\r\n");
    out.append(" Global Heap Total: ");
    out.append_uint(g_total_allocated);
    out.append(" bytes in ");
    out.append_uint(g_active_alloc_count);
    out.append(" total allocation(s)\r\n");
    out.append("======================================================================\r\n");
    emit(out);

    mem_lock_release();

#if MEM_CAPTURE_STACK
    if (sym_ready && pSymCleanup) pSymCleanup(hProcess);
    if (hDbgHelp) FreeLibrary(hDbgHelp);
#endif
    CloseHandle(hFile);
#endif
}

void mem_update_background(float dt, float interval_sec, const char* filepath) {
    if (interval_sec <= 0.0f) interval_sec = 5.0f;
    g_bg_timer += dt;
    if (g_bg_timer >= interval_sec) {
        g_bg_timer = 0.0f;
        mem_dump_leaks_to_file(filepath ? filepath : "memory_live.txt", 0);
    }
}

void mem_report_leaks() {
    mem_dump_leaks_to_file("memory_shutdown_leaks.txt", 0);
}

// ── CRT Shims ─────────────────────────────────────────────────────────────────
void* __cdecl malloc(size_t size) { return raw_heap_alloc_tracked(size, 0); }
EXPORT_IMP(malloc);

void __cdecl free(void* ptr) { raw_heap_free_tracked(ptr); }
EXPORT_IMP(free);

void* __cdecl calloc(size_t num, size_t size) {
    if (size != 0 && num > (SIZE_MAX / size)) return nullptr;
    size_t total = num * size;
    void* p = raw_heap_alloc_tracked(total, 0);
    if (p && total > 0) memset(p, 0, total);
    return p;
}
EXPORT_IMP(calloc);

void* __cdecl realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) { free(ptr); return nullptr; }

    AllocHeader* header = reinterpret_cast<AllocHeader*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(AllocHeader));
    if (header->magic == ALLOC_MAGIC_ACTIVE) {
        size_t old_size = header->size;
        void* new_p = raw_heap_alloc_tracked(new_size, 0);
        if (new_p) {
            size_t copy_len = old_size < new_size ? old_size : new_size;
            memcpy(new_p, ptr, copy_len);
            raw_heap_free_tracked(ptr);
        }
        return new_p;
    }
    return malloc(new_size);
}
EXPORT_IMP(realloc);

void* __cdecl _aligned_malloc(size_t size, size_t alignment) { return raw_heap_alloc_tracked(size, alignment); }
EXPORT_IMP(_aligned_malloc);

void __cdecl _aligned_free(void* ptr) { raw_heap_free_tracked(ptr); }
EXPORT_IMP(_aligned_free);

void* __mingw_aligned_malloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }
void  __mingw_aligned_free(void* ptr)                       { _aligned_free(ptr); }

} // extern "C"

// ── C++ Global operator new / delete Overloads ────────────────────────────────
void* operator new(size_t n)                                                       { return raw_heap_alloc_tracked(n, 0); }
void* operator new[](size_t n)                                                     { return operator new(n); }
void* operator new(size_t n, std::align_val_t al)                                  { return raw_heap_alloc_tracked(n, static_cast<size_t>(al)); }
void* operator new[](size_t n, std::align_val_t al)                                { return operator new(n, al); }
void* operator new(size_t n, const std::nothrow_t&) noexcept                       { return raw_heap_alloc_tracked(n, 0); }
void* operator new[](size_t n, const std::nothrow_t& tag) noexcept                 { return operator new(n, tag); }
void* operator new(size_t n, std::align_val_t al, const std::nothrow_t&) noexcept  { return raw_heap_alloc_tracked(n, static_cast<size_t>(al)); }
void* operator new[](size_t n, std::align_val_t al, const std::nothrow_t& tag) noexcept{ return operator new(n, al, tag); }

void operator delete(void* p) noexcept                                             { raw_heap_free_tracked(p); }
void operator delete[](void* p) noexcept                                           { operator delete(p); }
void operator delete(void* p, size_t) noexcept                                     { operator delete(p); }
void operator delete[](void* p, size_t) noexcept                                   { operator delete(p); }
void operator delete(void* p, std::align_val_t) noexcept                           { operator delete(p); }
void operator delete[](void* p, std::align_val_t) noexcept                         { operator delete(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept                    { operator delete(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept                  { operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept                      { operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept                    { operator delete(p); }