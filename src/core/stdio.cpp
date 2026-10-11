// stdio.cpp — Zero-CRT Stdio, Formatting, File I/O & Path Operations

#include "internal.h"

#ifdef _WIN32

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wattributes"
#  pragma GCC diagnostic ignored "-Wattribute-alias"
#endif

// Symbol decoration prefix for GCC __attribute__((alias))
#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
#  define RT_ASM_NAME(s) s
#else
#  define RT_ASM_NAME(s) "_" s
#endif

// Macro to cleanly define x86_64 / x86 __imp_ function pointers in .rdata
#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
  #define EXPORT_IMP(fn) extern "C" __attribute__((used)) auto const __imp_##fn = &fn
#else
  #define EXPORT_IMP(fn) extern "C" __attribute__((used)) auto const __imp_##fn = &fn; \
                         extern "C" __attribute__((used)) auto const __imp__##fn = &fn
#endif

extern "C" {

// ── Standard Streams ──────────────────────────────────────────────────────────
LocalFile g_std_files[3] = {
    { INVALID_HANDLE_VALUE, true },
    { INVALID_HANDLE_VALUE, true },
    { INVALID_HANDLE_VALUE, true }
};

#ifdef __acrt_iob_func
#  undef __acrt_iob_func
#endif

FILE* __cdecl __acrt_iob_func(unsigned int index) {
    if (index > 2) return nullptr;
    if (g_std_files[index].hFile == INVALID_HANDLE_VALUE) {
        DWORD stdH = (index == 0) ? STD_INPUT_HANDLE : (index == 1) ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
        g_std_files[index].hFile = GetStdHandle(stdH);
    }
    return reinterpret_cast<FILE*>(&g_std_files[index]);
}
EXPORT_IMP(__acrt_iob_func);

FILE* __cdecl __iob_func(void) {
    return __acrt_iob_func(0);
}
EXPORT_IMP(__iob_func);

// ── Safe Dynamic DLL Formatter Loader ─────────────────────────────────────────
typedef int (__cdecl * pfn_stdio_common_vsprintf)(
    unsigned long long options,
    char*              buffer,
    size_t             buffer_count,
    const char*        format,
    void*              locale,
    va_list            arglist
);
typedef int (__cdecl * pfn_vsnprintf)(char*, size_t, const char*, va_list);

static pfn_stdio_common_vsprintf g_pfn_ucrt_vsprintf    = nullptr;
static pfn_vsnprintf             g_pfn_legacy_vsnprintf = nullptr;
static bool                      g_vsnprintf_ready      = false;

static void init_vsnprintf() {
    if (g_vsnprintf_ready) return;
    g_vsnprintf_ready = true;

    // 1. Primary: Universal CRT (ucrtbase.dll) via __stdio_common_vsprintf
    HMODULE hUcrt = GetModuleHandleA("ucrtbase.dll");
    if (!hUcrt) hUcrt = LoadLibraryA("ucrtbase.dll");
    if (hUcrt) {
        g_pfn_ucrt_vsprintf = reinterpret_cast<pfn_stdio_common_vsprintf>(
            reinterpret_cast<void*>(GetProcAddress(hUcrt, "__stdio_common_vsprintf"))
        );
        if (g_pfn_ucrt_vsprintf) return;
    }

    // 2. Secondary: Legacy msvcrt.dll
    HMODULE hMsvcrt = GetModuleHandleA("msvcrt.dll");
    if (!hMsvcrt) hMsvcrt = LoadLibraryA("msvcrt.dll");
    if (hMsvcrt) {
        g_pfn_legacy_vsnprintf = reinterpret_cast<pfn_vsnprintf>(
            reinterpret_cast<void*>(GetProcAddress(hMsvcrt, "vsnprintf"))
        );
        if (!g_pfn_legacy_vsnprintf) {
            g_pfn_legacy_vsnprintf = reinterpret_cast<pfn_vsnprintf>(
                reinterpret_cast<void*>(GetProcAddress(hMsvcrt, "_vsnprintf"))
            );
        }
        if (g_pfn_legacy_vsnprintf) return;
    }

    // 3. Tertiary: ntdll.dll
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) hNtdll = LoadLibraryA("ntdll.dll");
    if (hNtdll) {
        g_pfn_legacy_vsnprintf = reinterpret_cast<pfn_vsnprintf>(
            reinterpret_cast<void*>(GetProcAddress(hNtdll, "_vsnprintf"))
        );
    }
}

// Primary formatting routine
int __cdecl vsnprintf(char* buf, size_t count, const char* fmt, va_list args) {
    if (!fmt) return -1;
    if (!g_vsnprintf_ready) init_vsnprintf();

    if (g_pfn_ucrt_vsprintf) {
        return g_pfn_ucrt_vsprintf(0x2ULL, buf, count, fmt, nullptr, args);
    }

    if (g_pfn_legacy_vsnprintf) {
        int r = g_pfn_legacy_vsnprintf(buf, count, fmt, args);
        if (buf && count > 0) {
            if (r < 0 || static_cast<size_t>(r) >= count) {
                buf[count - 1] = '\0';
            } else {
                buf[r] = '\0';
            }
        }
        return r;
    }

    // Bare fallback if no dynamic CRT is available
    if (buf && count > 0) {
        size_t len = 0;
        while (fmt[len] && len < count - 1) {
            buf[len] = fmt[len];
            len++;
        }
        buf[len] = '\0';
        return static_cast<int>(len);
    }
    return -1;
}

// Zero-overhead aliases for vsnprintf
int __cdecl __mingw_vsnprintf(char* buf, size_t count, const char* fmt, va_list args)
    __attribute__((alias(RT_ASM_NAME("vsnprintf"))));

// Primary snprintf routine
int __cdecl snprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vsnprintf(buf, n, fmt, args);
    va_end(args);
    return r;
}

// Zero-overhead alias for __mingw_snprintf
int __cdecl __mingw_snprintf(char* buf, size_t n, const char* fmt, ...)
    __attribute__((alias(RT_ASM_NAME("snprintf"))));

FILE* __cdecl fopen(const char* filename, const char* mode) {
    if (!filename || !mode) return nullptr;

    const bool plus = strchr(mode, '+') != nullptr;
    DWORD access = 0, creation = 0;

    switch (mode[0]) {
        case 'r':
            creation = OPEN_EXISTING;
            access   = GENERIC_READ | (plus ? GENERIC_WRITE : 0);
            break;
        case 'w':
            creation = CREATE_ALWAYS;
            access   = GENERIC_WRITE | (plus ? GENERIC_READ : 0);
            break;
        case 'a':
            creation = OPEN_ALWAYS;
            access   = FILE_APPEND_DATA | (plus ? GENERIC_READ : 0);
            break;
        default:
            return nullptr;
    }

    HANDLE h = CreateFileA(filename, access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;

    LocalFile* lf = static_cast<LocalFile*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LocalFile)));
    if (!lf) { CloseHandle(h); return nullptr; }
    lf->hFile     = h;
    lf->isConsole = false;
    return reinterpret_cast<FILE*>(lf);
}
EXPORT_IMP(fopen);

int __cdecl fclose(FILE* stream) {
    if (!stream) return -1;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);
    if (lf >= &g_std_files[0] && lf <= &g_std_files[2]) return 0;
    if (lf->hFile != INVALID_HANDLE_VALUE) CloseHandle(lf->hFile);
    HeapFree(GetProcessHeap(), 0, lf);
    return 0;
}
EXPORT_IMP(fclose);

size_t __cdecl fread(void* ptr, size_t size, size_t count, FILE* stream) {
    if (!ptr || !stream || size == 0 || count == 0) return 0;
    if (count > SIZE_MAX / size) return 0;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);

    const size_t total = size * count;
    char*  dst  = static_cast<char*>(ptr);
    size_t done = 0;

    while (done < total) {
        size_t left  = total - done;
        DWORD  chunk = left > 0x40000000u ? 0x40000000u : static_cast<DWORD>(left);
        DWORD  got   = 0;
        if (!ReadFile(lf->hFile, dst + done, chunk, &got, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) lf->eof = true;
            else                                     lf->err = true;
            break;
        }
        if (got == 0) { lf->eof = true; break; }
        done += got;
        if (lf->isConsole) break;           // console reads return per line
    }
    return done / size;
}
EXPORT_IMP(fread);

size_t __cdecl fwrite(const void* ptr, size_t size, size_t count, FILE* stream) {
    if (!ptr || !stream || size == 0 || count == 0) return 0;
    if (count > SIZE_MAX / size) return 0;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);

    // std streams may not have had their handle resolved yet
    if (lf >= &g_std_files[0] && lf <= &g_std_files[2])
        __acrt_iob_func(static_cast<unsigned>(lf - g_std_files));

    const size_t total = size * count;
    const char*  src   = static_cast<const char*>(ptr);
    size_t done = 0;

    while (done < total) {
        size_t left  = total - done;
        DWORD  chunk = left > 0x40000000u ? 0x40000000u : static_cast<DWORD>(left);
        DWORD  put   = 0;
        if (!WriteFile(lf->hFile, src + done, chunk, &put, nullptr) || put == 0) {
            lf->err = true;
            break;
        }
        done += put;
    }
    return done / size;
}
EXPORT_IMP(fwrite);

int __cdecl fseek(FILE* stream, long offset, int origin) {
    if (!stream) return -1;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);
    DWORD method = (origin == 1) ? FILE_CURRENT : (origin == 2) ? FILE_END : FILE_BEGIN;

    LARGE_INTEGER li; li.QuadPart = offset;
    if (!SetFilePointerEx(lf->hFile, li, nullptr, method)) return -1;
    lf->eof = false;
    return 0;
}
EXPORT_IMP(fseek);

long __cdecl ftell(FILE* stream) {
    if (!stream) return -1L;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);
    LARGE_INTEGER zero = {}, pos = {};
    if (!SetFilePointerEx(lf->hFile, zero, &pos, FILE_CURRENT)) return -1L;
    return static_cast<long>(pos.QuadPart);
}
EXPORT_IMP(ftell);

void __cdecl rewind(FILE* stream) {
    if (!stream) return;
    fseek(stream, 0L, 0);
    reinterpret_cast<LocalFile*>(stream)->err = false;
}
EXPORT_IMP(rewind);

int __cdecl fflush(FILE* stream) {
    if (!stream) return 0;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);
    if (lf->hFile != INVALID_HANDLE_VALUE && !lf->isConsole) FlushFileBuffers(lf->hFile);
    return 0;
}
EXPORT_IMP(fflush);

int __cdecl feof(FILE* stream) {
    return stream ? (reinterpret_cast<LocalFile*>(stream)->eof ? 1 : 0) : 1;
}
EXPORT_IMP(feof);

int __cdecl ferror(FILE* stream) {
    return stream ? (reinterpret_cast<LocalFile*>(stream)->err ? 1 : 0) : 1;
}
EXPORT_IMP(ferror);

int __cdecl fputs(const char* str, FILE* stream) {
    if (!str || !stream) return -1;
    size_t len = strlen(str);
    return (fwrite(str, 1, len, stream) == len) ? 0 : -1;
}
EXPORT_IMP(fputs);

char* __cdecl fgets(char* str, int num, FILE* stream) {
    if (!str || num <= 1 || !stream) return nullptr;
    int i = 0;
    while (i < num - 1) {
        char c;
        if (fread(&c, 1, 1, stream) != 1) {
            if (i == 0) return nullptr;
            break;
        }
        str[i++] = c;
        if (c == '\n') break;
    }
    str[i] = '\0';
    return str;
}
EXPORT_IMP(fgets);

int __cdecl fgetc(FILE* stream) {
    if (!stream) return -1;
    unsigned char c = 0;
    if (fread(&c, 1, 1, stream) != 1) return -1;
    return static_cast<int>(c);
}
EXPORT_IMP(fgetc);

int __cdecl getc(FILE* stream) __attribute__((alias(RT_ASM_NAME("fgetc"))));
EXPORT_IMP(getc);

int __cdecl fputc(int c, FILE* stream) {
    if (!stream) return -1;
    unsigned char ch = static_cast<unsigned char>(c);
    if (fwrite(&ch, 1, 1, stream) != 1) return -1;
    return static_cast<int>(ch);
}
EXPORT_IMP(fputc);

int __cdecl putc(int c, FILE* stream) __attribute__((alias(RT_ASM_NAME("fputc"))));
EXPORT_IMP(putc);

int __cdecl puts(const char* str) {
    if (!str) return -1;
    size_t len = strlen(str);
    DWORD written;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(hOut, str, static_cast<DWORD>(len), &written, nullptr);
    WriteFile(hOut, "\r\n", 2, &written, nullptr);
    return 0;
}
EXPORT_IMP(puts);

int __cdecl putchar(int c) {
    return fputc(c, reinterpret_cast<FILE*>(&g_std_files[1]));
}
EXPORT_IMP(putchar);

int __cdecl getchar(void) {
    return fgetc(reinterpret_cast<FILE*>(&g_std_files[0]));
}
EXPORT_IMP(getchar);

// ── Path / Filesystem Shims ───────────────────────────────────────────────────
char* __cdecl _fullpath(char* absPath, const char* relPath, size_t maxLength) {
    const char* path = (relPath && *relPath) ? relPath : ".";
    DWORD reqLen = GetFullPathNameA(path, 0, nullptr, nullptr);
    if (reqLen == 0) return nullptr;

    char* buf = absPath;
    if (!buf) {
        size_t allocSize = (maxLength > reqLen) ? maxLength : (reqLen + 1);
        if (allocSize < MAX_PATH) allocSize = MAX_PATH;
        buf = static_cast<char*>(malloc(allocSize));
        if (!buf) return nullptr;
        maxLength = allocSize;
    } else {
        if (maxLength > 0 && reqLen > maxLength) return nullptr;
    }

    DWORD res = GetFullPathNameA(path, static_cast<DWORD>(maxLength ? maxLength : reqLen + 1), buf, nullptr);
    if (res == 0 || (maxLength > 0 && res > maxLength)) {
        if (!absPath) free(buf);
        return nullptr;
    }
    return buf;
}
EXPORT_IMP(_fullpath);

int __cdecl _mkdir(const char* dirname) {
    if (!dirname) return -1;
    if (CreateDirectoryA(dirname, nullptr)) return 0;
    return (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -1;
}
EXPORT_IMP(_mkdir);

int __cdecl remove(const char* filename) {
    if (!filename) return -1;
    if (DeleteFileA(filename)) return 0;
    if (RemoveDirectoryA(filename)) return 0;
    return -1;
}
EXPORT_IMP(remove);

int __cdecl _unlink(const char* filename) __attribute__((alias(RT_ASM_NAME("remove"))));
EXPORT_IMP(_unlink);

int __cdecl unlink(const char* filename) __attribute__((alias(RT_ASM_NAME("remove"))));
EXPORT_IMP(unlink);

int __cdecl rename(const char* oldname, const char* newname) {
    if (!oldname || !newname) return -1;
    return MoveFileExA(oldname, newname, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) ? 0 : -1;
}
EXPORT_IMP(rename);

// ── Printf Formatting Helpers ─────────────────────────────────────────────────
static void rt_file_write(FILE* stream, const char* buf, int len) {
    if (!stream || len <= 0) return;
    LocalFile* lf = reinterpret_cast<LocalFile*>(stream);
    if (lf == &g_std_files[0]) {
        DWORD w; WriteFile(GetStdHandle(STD_INPUT_HANDLE), buf, static_cast<DWORD>(len), &w, nullptr);
    } else if (lf == &g_std_files[1]) {
        DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, static_cast<DWORD>(len), &w, nullptr);
    } else if (lf == &g_std_files[2]) {
        DWORD w; WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, static_cast<DWORD>(len), &w, nullptr);
    } else if (lf->hFile != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(lf->hFile, buf, static_cast<DWORD>(len), &w, nullptr);
    }
}

int vfprintf(FILE* stream, const char* fmt, va_list args) {
    if (!fmt) return -1;
    char stack_buf[2048];
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    if (len >= 0 && static_cast<size_t>(len) < sizeof(stack_buf)) {
        rt_file_write(stream ? stream : reinterpret_cast<FILE*>(&g_std_files[1]), stack_buf, len);
        va_end(args_copy);
        return len;
    }
    if (len > 0) {
        char* heap_buf = static_cast<char*>(malloc(len + 1));
        if (heap_buf) {
            vsnprintf(heap_buf, len + 1, fmt, args_copy);
            rt_file_write(stream ? stream : reinterpret_cast<FILE*>(&g_std_files[1]), heap_buf, len);
            free(heap_buf);
        }
    }
    va_end(args_copy);
    return len;
}

int fprintf(FILE* stream, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vfprintf(stream ? stream : reinterpret_cast<FILE*>(&g_std_files[2]), fmt, args);
    va_end(args);
    return r;
}

int __mingw_fprintf(FILE* stream, const char* fmt, ...)
    __attribute__((alias(RT_ASM_NAME("fprintf"))));

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int r = vfprintf(reinterpret_cast<FILE*>(&g_std_files[1]), fmt, args);
    va_end(args);
    return r;
}

int __mingw_printf(const char* fmt, ...)
    __attribute__((alias(RT_ASM_NAME("printf"))));

int vprintf(const char* fmt, va_list args) {
    return vfprintf(reinterpret_cast<FILE*>(&g_std_files[1]), fmt, args);
}

// ── UCRT Common Formatting Shims ──────────────────────────────────────────────
int __stdio_common_vsprintf(unsigned long long options, char* buffer, size_t buffer_count, const char* format, _locale_t locale, va_list arglist) {
    if (!format) return -1;
    if (!g_vsnprintf_ready) init_vsnprintf();
    if (g_pfn_ucrt_vsprintf) {
        return g_pfn_ucrt_vsprintf(options | 0x2ULL, buffer, buffer_count, format, locale, arglist);
    }
    return vsnprintf(buffer, buffer_count, format, arglist);
}
EXPORT_IMP(__stdio_common_vsprintf);

int __cdecl __stdio_common_vfprintf(unsigned long long /*options*/, FILE* stream, const char* format, _locale_t /*locale*/, va_list arglist) {
    return vfprintf(stream, format, arglist);
}
EXPORT_IMP(__stdio_common_vfprintf);

} // extern "C"

#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

#endif // _WIN32