#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #define _GNU_SOURCE
  #include <unistd.h>
  #include <sys/mman.h>
  #include <sys/syscall.h>
#endif

/* ── malloc / free for lzmadec (no libc) ─────────────────────────────────── */
#ifdef _WIN32
void *malloc(size_t s) { return VirtualAlloc(NULL, s, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE); }
void  free(void *p)    { if (p) VirtualFree(p, 0, MEM_RELEASE); }
#endif

#include "lzmadec.h"
#include "payload.h"

/* ── Windows: in-memory PE loader ────────────────────────────────────────── */
#ifdef _WIN32

static void run_pe(unsigned char *data) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)data;
    IMAGE_NT_HEADERS *nt  = (IMAGE_NT_HEADERS *)(data + dos->e_lfanew);

    DWORD imageBase = nt->OptionalHeader.ImageBase;
    DWORD imageSize = nt->OptionalHeader.SizeOfImage;

    /* Allocate at preferred base, fall back to anywhere */
    unsigned char *base = (unsigned char *)VirtualAlloc(
        (LPVOID)imageBase, imageSize, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!base)
        base = (unsigned char *)VirtualAlloc(
            NULL, imageSize, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    /* Copy headers */
    for (DWORD i = 0; i < nt->OptionalHeader.SizeOfHeaders; i++)
        base[i] = data[i];

    /* Copy sections */
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        unsigned char *dst = base + sec[i].VirtualAddress;
        unsigned char *src = data + sec[i].PointerToRawData;
        for (DWORD j = 0; j < sec[i].SizeOfRawData; j++)
            dst[j] = src[j];
    }

    /* Relocations */
    DWORD delta = (DWORD)base - imageBase;
    if (delta) {
        IMAGE_DATA_DIRECTORY *dir =
            &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (dir->Size) {
            IMAGE_BASE_RELOCATION *rel =
                (IMAGE_BASE_RELOCATION *)(base + dir->VirtualAddress);
            while (rel->VirtualAddress) {
                WORD  *entry = (WORD *)((unsigned char *)rel + sizeof(IMAGE_BASE_RELOCATION));
                DWORD  count = (rel->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                for (DWORD i = 0; i < count; i++) {
                    if ((entry[i] >> 12) == IMAGE_REL_BASED_HIGHLOW) {
                        DWORD *patch =
                            (DWORD *)(base + rel->VirtualAddress + (entry[i] & 0xFFF));
                        *patch += delta;
                    }
                }
                rel = (IMAGE_BASE_RELOCATION *)((unsigned char *)rel + rel->SizeOfBlock);
            }
        }
    }

    /* Imports */
    IMAGE_DATA_DIRECTORY *idir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (idir->Size) {
        IMAGE_IMPORT_DESCRIPTOR *imp =
            (IMAGE_IMPORT_DESCRIPTOR *)(base + idir->VirtualAddress);
        while (imp->Name) {
            HMODULE mod = LoadLibraryA((char *)(base + imp->Name));
            if (!mod) {
                // Fail fast if a dependency is missing (e.g. XInput in a sandbox)
                ExitProcess(0xDEADC0DE);
            }
            IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA *)(base +
                (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
            while (orig->u1.AddressOfData) {
                FARPROC func = NULL;
                if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                    func = GetProcAddress(mod, (LPCSTR)IMAGE_ORDINAL(orig->u1.Ordinal));
                else {
                    IMAGE_IMPORT_BY_NAME *ibn =
                        (IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData);
                    func = GetProcAddress(mod, ibn->Name);
                }
                if (!func) {
                    // Function missing in the DLL
                    ExitProcess(0xDEADBEEF);
                }
                iat->u1.Function = (DWORD)func;
                orig++; iat++;
            }
            imp++;
        }
    }

    /* Patch PEB ImageBaseAddress so GetModuleHandle(NULL) returns our base.
       For console apps this ensures the CRT finds its own resources/strings.
       For GUI apps this makes RegisterClassEx / LoadIcon work correctly. */
    DWORD peb;
    __asm__ volatile ("mov %%fs:0x30, %0" : "=r"(peb));
    *(DWORD *)(peb + 0x08) = (DWORD)base;

    /* Jump to entry point with 16-byte stack alignment for SSE2 */
    typedef void (__stdcall *EP)(void);
    EP entry = (EP)(base + nt->OptionalHeader.AddressOfEntryPoint);

    __asm__ volatile (
        "andl $-16, %%esp\n"
        "subl $16, %%esp\n"
        "call *%0\n"
        : : "r"(entry)
    );
}

/* Both entry point names are provided:
   - WinMainCRTStartup  → --subsystem,windows  (GUI)
   - mainCRTStartup     → --subsystem,console  (console)
   packer.py detects the payload subsystem and passes the right --subsystem
   flag; the linker selects the matching entry point automatically. */
void __attribute__((noreturn)) WinMainCRTStartup(void);
void __attribute__((noreturn, alias("WinMainCRTStartup"))) mainCRTStartup(void);

void __attribute__((noreturn)) WinMainCRTStartup(void) {
    unsigned char *buf = (unsigned char *)VirtualAlloc(
        NULL, PAYLOAD_ORIGINAL_SIZE, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);

    size_t src_size = PAYLOAD_COMPRESSED_SIZE;
    size_t dst_size = PAYLOAD_ORIGINAL_SIZE;
    decode_lzma(payload, &src_size, buf, &dst_size);

    run_pe(buf);
    ExitProcess(0);
}

/* ── Linux: fileless memfd ───────────────────────────────────────────────── */
#else

#if defined(__i386__)
  #define REG_SP      "%%esp"
  #define SYS_MMAP_NR 192 /* mmap2 */
#else
  #define REG_SP      "%%rsp"
  #define SYS_MMAP_NR 9   /* mmap */
#endif

// syscall numbers if not defined by headers (minimal env)
#ifndef SYS_memfd_create
  #if defined(__i386__)
    #define SYS_memfd_create 356
    #define SYS_write        4
    #define SYS_exit         1
    #define SYS_execveat     358
    #define SYS_execve       11
    #define SYS_open         5
    #define SYS_chmod        15
    #define SYS_unlink       10
  #else
    #define SYS_memfd_create 319
    #define SYS_write        1
    #define SYS_exit         60
    #define SYS_execveat     322
    #define SYS_execve       59
    #define SYS_open         2
    #define SYS_chmod        90
    #define SYS_unlink       87
  #endif
#endif

// Manual syscall wrapper for -nostdlib
static long rt_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long res;
#if defined(__i386__)
    __asm__ volatile (
        "xchgl %%ebp, %1\n"
        "int $0x80\n"
        "xchgl %%ebp, %1\n"
        : "=a"(res), "+r"(a6)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory"
    );
#else
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall"
        : "=a"(res)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
#endif
    return res;
}

void *malloc(size_t s) { 
    return (void *)rt_syscall(SYS_MMAP_NR, 0, (long)s, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0); 
}
void free(void *p) { (void)p; }

static void __attribute__((optimize("no-tree-loop-distribute-patterns"))) rt_puts(const char* s) {
    int len = 0; while(s[len]) len++;
    rt_syscall(SYS_write, 2, (long)s, (long)len, 0, 0, 0);
}

static int rt_itoa(int n, char* s) {
    int i = 0;
    if (n == 0) { s[i++] = '0'; s[i] = '\0'; return i; }
    if (n < 0) { s[i++] = '-'; n = -n; }
    int start = i;
    while (n > 0) { s[i++] = (n % 10) + '0'; n /= 10; }
    for (int j = 0; j < (i-start)/2; ++j) { char t = s[start+j]; s[start+j] = s[i-j-1]; s[i-j-1] = t; }
    s[i] = '\0';
    return i;
}

static long *g_stack;

/* Move stub state to globals to avoid stack usage */
static unsigned char *g_buf;
static size_t g_src_size, g_dst_size;
static int g_fd;
static char g_path[64];
static char g_code[16];

void __attribute__((naked, noreturn)) _start(void) {
    __asm__ volatile (
#if defined(__i386__)
        "movl %%esp, %%eax\n"
        "pushl %%eax\n"
        "call _start_c\n"
#else
        "movq %%rsp, %%rdi\n"
        "call _start_c\n"
#endif
        : : : "memory"
    );
    while(1);
}

void __attribute__((noreturn, used, visibility("default"))) _start_c(long *stack) {
    int argc = (int)stack[0];
    char **argv = (char **)&stack[1];
    
    char **envp = argv;
    while (*envp != NULL) envp++;
    envp++;

    g_buf = (unsigned char *)rt_syscall(
        SYS_MMAP_NR, 0, (long)PAYLOAD_ORIGINAL_SIZE,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

    if ((long)g_buf == -1) rt_syscall(SYS_exit, 1, 0, 0, 0, 0, 0);

    g_src_size = PAYLOAD_COMPRESSED_SIZE;
    g_dst_size = PAYLOAD_ORIGINAL_SIZE;
    decode_lzma(payload, &g_src_size, g_buf, &g_dst_size);

    /* 1. Try memfd_create (preferred, fileless) */
    g_fd = (int)rt_syscall(SYS_memfd_create, (long)"rt_run", 1, 0, 0, 0, 0);
    
    /* 2. Fallback to /tmp if memfd fails */
    if (g_fd < 0) {
        g_fd = (int)rt_syscall(SYS_open, (long)"/tmp/.rt_rterm", 0x42, 0700, 0, 0, 0);
        if (g_fd < 0) {
            /* 3. Last resort: current directory (useful for restricted sandboxes) */
            g_fd = (int)rt_syscall(SYS_open, (long)".rt_run", 0x42, 0700, 0, 0, 0);
        }
        if (g_fd >= 0) {
            rt_syscall(SYS_chmod, (long)"/tmp/.rt_rterm", 0700, 0, 0, 0, 0);
            rt_syscall(SYS_chmod, (long)".rt_run", 0700, 0, 0, 0, 0);
        }
    }

    if (g_fd < 0) rt_syscall(SYS_exit, 1, 0, 0, 0, 0, 0);

    rt_syscall(SYS_write, (long)g_fd, (long)g_buf, (long)g_dst_size, 0, 0, 0);

    /* Try execveat (3.19+) */
    rt_syscall(SYS_execveat, (long)g_fd, (long)"", (long)argv, (long)envp, 0x1000, 0); 
    
    /* Double-check fallback for earlier kernels */
    g_path[0] = '/'; g_path[1] = 'p'; g_path[2] = 'r'; g_path[3] = 'o'; g_path[4] = 'c';
    g_path[5] = '/'; g_path[6] = 's'; g_path[7] = 'e'; g_path[8] = 'l'; g_path[9] = 'f';
    g_path[10] = '/'; g_path[11] = 'f'; g_path[12] = 'd'; g_path[13] = '/';
    rt_itoa(g_fd, g_path + 14);
    
    rt_syscall(SYS_execve, (long)g_path, (long)argv, (long)envp, 0, 0, 0);

    /* If we are here, everything failed - delete temp if it exists and exit */
    rt_syscall(SYS_unlink, (long)"/tmp/.rt_rterm", 0, 0, 0, 0, 0);
    rt_syscall(SYS_exit, 1, 0, 0, 0, 0, 0);
    while(1);
}


#endif