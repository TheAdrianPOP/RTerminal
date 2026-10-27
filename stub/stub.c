#ifdef _WIN32
  #include <windows.h>
#else
  #define _GNU_SOURCE
  #include <stddef.h>
#endif

#include "lzmadec.h"
#include "payload.h"

/* ── Windows: in-memory PE loader ────────────────────────────────────────── */
#ifdef _WIN32

__attribute__((section(".text"), used))
static const char g_packer_sig[] = "RTPACK_V1";

static void run_pe(unsigned char *data) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)data;
    IMAGE_NT_HEADERS *nt  = (IMAGE_NT_HEADERS *)(data + dos->e_lfanew);

    ULONG_PTR imageBase = nt->OptionalHeader.ImageBase;
    DWORD imageSize = nt->OptionalHeader.SizeOfImage;

    IMAGE_DATA_DIRECTORY *reloc_dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    int has_reloc = (reloc_dir->Size && reloc_dir->VirtualAddress);

    /* Prefer OS-chosen random address (ASLR) rather than fixed imageBase */
    unsigned char *base = NULL;
    if (has_reloc) {
        base = (unsigned char *)VirtualAlloc(
            NULL, imageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    }
    if (!base) {
        base = (unsigned char *)VirtualAlloc(
            (LPVOID)imageBase, imageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    }
    if (!base) {
        base = (unsigned char *)VirtualAlloc(
            NULL, imageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    }
    if (!base) ExitProcess(1);

    for (DWORD i = 0; i < nt->OptionalHeader.SizeOfHeaders; i++)
        base[i] = data[i];

    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!sec[i].VirtualAddress) continue;
        unsigned char *dst = base + sec[i].VirtualAddress;
        if (sec[i].PointerToRawData && sec[i].SizeOfRawData) {
            unsigned char *src = data + sec[i].PointerToRawData;
            for (DWORD j = 0; j < sec[i].SizeOfRawData; j++)
                dst[j] = src[j];
        }
    }

    ULONG_PTR delta = (ULONG_PTR)base - imageBase;
    if (delta) {
        IMAGE_DATA_DIRECTORY *dir =
            &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (dir->Size && dir->VirtualAddress) {
            unsigned char *rel_end = base + dir->VirtualAddress + dir->Size;
            IMAGE_BASE_RELOCATION *rel =
                (IMAGE_BASE_RELOCATION *)(base + dir->VirtualAddress);
            while ((unsigned char *)rel < rel_end && rel->SizeOfBlock > 0) {
                WORD  *entry = (WORD *)((unsigned char *)rel + sizeof(IMAGE_BASE_RELOCATION));
                DWORD  count = (rel->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                for (DWORD i = 0; i < count; i++) {
                    int type = entry[i] >> 12;
                    int offset = entry[i] & 0xFFF;
#ifdef _WIN64
                    if (type == 10) {
                        ULONG_PTR *patch = (ULONG_PTR *)(base + rel->VirtualAddress + offset);
                        *patch += delta;
                    }
#else
                    if (type == 3) {
                        DWORD *patch = (DWORD *)(base + rel->VirtualAddress + offset);
                        *patch += (DWORD)delta;
                    }
#endif
                }
                rel = (IMAGE_BASE_RELOCATION *)((unsigned char *)rel + rel->SizeOfBlock);
            }
        }
    }

    IMAGE_DATA_DIRECTORY *idir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (idir->Size && idir->VirtualAddress) {
        IMAGE_IMPORT_DESCRIPTOR *imp =
            (IMAGE_IMPORT_DESCRIPTOR *)(base + idir->VirtualAddress);
        while (imp->Name) {
            HMODULE mod = LoadLibraryA((char *)(base + imp->Name));
            if (!mod) ExitProcess(1);

            IMAGE_THUNK_DATA *orig = (IMAGE_THUNK_DATA *)(base +
                (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
            while (orig->u1.AddressOfData) {
                FARPROC func = NULL;
#ifdef _WIN64
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                    func = GetProcAddress(mod, (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
#else
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
                    func = GetProcAddress(mod, (LPCSTR)(orig->u1.Ordinal & 0xFFFF));
#endif
                else {
                    IMAGE_IMPORT_BY_NAME *ibn =
                        (IMAGE_IMPORT_BY_NAME *)(base + orig->u1.AddressOfData);
                    func = GetProcAddress(mod, ibn->Name);
                }
                if (!func) ExitProcess(1);
                iat->u1.Function = (ULONG_PTR)func;
                orig++;
                iat++;
            }
            imp++;
        }
    }

    /* Apply W^X memory protection per section based on PE characteristics */
    DWORD oldProtect;
    VirtualProtect(base, nt->OptionalHeader.SizeOfHeaders, PAGE_READONLY, &oldProtect);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!sec[i].VirtualAddress) continue;
        DWORD size = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (!size) continue;

        DWORD chars = sec[i].Characteristics;
        DWORD protect = PAGE_NOACCESS;

        if (chars & IMAGE_SCN_MEM_EXECUTE) {
            protect = (chars & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        } else if (chars & IMAGE_SCN_MEM_WRITE) {
            protect = PAGE_READWRITE;
        } else if (chars & IMAGE_SCN_MEM_READ) {
            protect = PAGE_READONLY;
        }

        VirtualProtect(base + sec[i].VirtualAddress, size, protect, &oldProtect);
    }

    FlushInstructionCache(GetCurrentProcess(), base, imageSize);

    IMAGE_DATA_DIRECTORY *tls_dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (tls_dir->Size && tls_dir->VirtualAddress) {
        IMAGE_TLS_DIRECTORY *tls =
            (IMAGE_TLS_DIRECTORY *)(base + tls_dir->VirtualAddress);
        if (tls->AddressOfCallBacks) {
            PIMAGE_TLS_CALLBACK *cb =
                (PIMAGE_TLS_CALLBACK *)(tls->AddressOfCallBacks);
            while (*cb) {
                (*cb)((PVOID)base, DLL_PROCESS_ATTACH, NULL);
                cb++;
            }
        }
    }

#ifdef _WIN64
    ULONG_PTR peb;
    __asm__ volatile ("mov %%gs:0x60, %0" : "=r"(peb));
    *(ULONG_PTR *)(peb + 0x10) = (ULONG_PTR)base;
#else
    ULONG_PTR peb;
    __asm__ volatile ("mov %%fs:0x30, %0" : "=r"(peb));
    *(ULONG_PTR *)(peb + 0x08) = (ULONG_PTR)base;
#endif

    typedef void (__stdcall *EP)(void);
    EP entry = (EP)(base + nt->OptionalHeader.AddressOfEntryPoint);

#ifdef _WIN64
    entry();
#else
    __asm__ volatile (
        "andl $-16, %%esp\n"
        "subl $12, %%esp\n"
        "pushl $0\n"
        "call *%0\n"
        : : "r"(entry)
        : "eax", "ecx", "edx", "memory"
    );
#endif
}

void __attribute__((noreturn)) WinMainCRTStartup(void);
void __attribute__((noreturn, alias("WinMainCRTStartup"))) mainCRTStartup(void);

void __attribute__((noreturn)) WinMainCRTStartup(void) {
    /* Allocate buffer for payload + 64 KB probability table (0 disk cost) */
    unsigned char *mem = (unsigned char *)VirtualAlloc(
        NULL, PAYLOAD_ORIGINAL_SIZE + 65536, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!mem) ExitProcess(1);

    unsigned char *buf = mem;
    uint16_t *probs = (uint16_t *)(mem + PAYLOAD_ORIGINAL_SIZE);

    size_t src_size = PAYLOAD_COMPRESSED_SIZE;
    size_t dst_size = PAYLOAD_ORIGINAL_SIZE;
    if (decode_lzma(payload, &src_size, buf, &dst_size, probs) != 0) {
        ExitProcess(1);
    }

#if PAYLOAD_USE_BCJ
    x86_decode(buf, dst_size);
#endif

    run_pe(buf);
    ExitProcess(0);
}

/* ── Linux: fileless memfd ───────────────────────────────────────────────── */
#else

#define PROT_READ     1
#define PROT_WRITE    2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

#if defined(__i386__)
  #define SYS_MMAP_NR      192
  #define SYS_memfd_create 356
  #define SYS_write        4
  #define SYS_exit         1
  #define SYS_execveat     358
  #define SYS_execve       11
  #define SYS_open         5
  #define SYS_chmod        15
  #define SYS_fchmod       94
  #define SYS_unlink       10
#else
  #define SYS_MMAP_NR      9
  #define SYS_memfd_create 319
  #define SYS_write        1
  #define SYS_exit         60
  #define SYS_execveat     322
  #define SYS_execve       59
  #define SYS_open         2
  #define SYS_chmod        90
  #define SYS_fchmod       91
  #define SYS_unlink       87
#endif

#ifdef __i386__
  #define _syscall1(num, a1) ({ \
      long __res; \
      __asm__ volatile("int $0x80" : "=a"(__res) : "a"(num), "b"((long)(a1)) : "memory"); \
      __res; \
  })
  #define _syscall2(num, a1, a2) ({ \
      long __res; \
      __asm__ volatile("int $0x80" : "=a"(__res) : "a"(num), "b"((long)(a1)), "c"((long)(a2)) : "memory"); \
      __res; \
  })
  #define _syscall3(num, a1, a2, a3) ({ \
      long __res; \
      __asm__ volatile("int $0x80" : "=a"(__res) : "a"(num), "b"((long)(a1)), "c"((long)(a2)), "d"((long)(a3)) : "memory"); \
      __res; \
  })
  #define _syscall5(num, a1, a2, a3, a4, a5) ({ \
      long __res; \
      __asm__ volatile("int $0x80" : "=a"(__res) : "a"(num), "b"((long)(a1)), "c"((long)(a2)), "d"((long)(a3)), "S"((long)(a4)), "D"((long)(a5)) : "memory"); \
      __res; \
  })

  static inline long _syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
      long res;
      struct { long a1, a2, a3, a4, a5, a6; } args = { a1, a2, a3, a4, a5, a6 };
      __asm__ volatile (
          "pushl %%ebp\n"
          "movl 20(%1), %%ebp\n"
          "movl 16(%1), %%edi\n"
          "movl 12(%1), %%esi\n"
          "movl 8(%1), %%edx\n"
          "movl 4(%1), %%ecx\n"
          "movl 0(%1), %%ebx\n"
          "int $0x80\n"
          "popl %%ebp\n"
          : "=a"(res)
          : "r"(&args), "0"(num)
          : "ebx", "ecx", "edx", "esi", "edi", "memory"
      );
      return res;
  }
#else /* __x86_64__ */
  #define _syscall1(num, a1) ({ \
      long __res; \
      __asm__ volatile("syscall" : "=a"(__res) : "a"(num), "D"((long)(a1)) : "rcx", "r11", "memory"); \
      __res; \
  })
  #define _syscall2(num, a1, a2) ({ \
      long __res; \
      __asm__ volatile("syscall" : "=a"(__res) : "a"(num), "D"((long)(a1)), "S"((long)(a2)) : "rcx", "r11", "memory"); \
      __res; \
  })
  #define _syscall3(num, a1, a2, a3) ({ \
      long __res; \
      __asm__ volatile("syscall" : "=a"(__res) : "a"(num), "D"((long)(a1)), "S"((long)(a2)), "d"((long)(a3)) : "rcx", "r11", "memory"); \
      __res; \
  })
  #define _syscall5(num, a1, a2, a3, a4, a5) ({ \
      long __res; \
      register long __r10 __asm__("r10") = (long)(a4); \
      register long __r8  __asm__("r8")  = (long)(a5); \
      __asm__ volatile("syscall" : "=a"(__res) : "a"(num), "D"((long)(a1)), "S"((long)(a2)), "d"((long)(a3)), "r"(__r10), "r"(__r8) : "rcx", "r11", "memory"); \
      __res; \
  })
  #define _syscall6(num, a1, a2, a3, a4, a5, a6) ({ \
      long __res; \
      register long __r10 __asm__("r10") = (long)(a4); \
      register long __r8  __asm__("r8")  = (long)(a5); \
      register long __r9  __asm__("r9")  = (long)(a6); \
      __asm__ volatile("syscall" : "=a"(__res) : "a"(num), "D"((long)(a1)), "S"((long)(a2)), "d"((long)(a3)), "r"(__r10), "r"(__r8), "r"(__r9) : "rcx", "r11", "memory"); \
      __res; \
  })
#endif

static void format_fd_path(char *path, int fd) {
    char *p = path + 14;
    int printed = 0;
    if (fd >= 1000) {
        *p++ = (fd / 1000) + '0';
        fd %= 1000;
        printed = 1;
    }
    if (fd >= 100 || printed) {
        *p++ = (fd / 100) + '0';
        fd %= 100;
        printed = 1;
    }
    if (fd >= 10 || printed) {
        *p++ = (fd / 10) + '0';
        fd %= 10;
    }
    *p++ = fd + '0';
    *p = '\0';
}

void __attribute__((noreturn, used, visibility("default"))) _start_c(long *stack);

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

    size_t alloc_sz = (size_t)PAYLOAD_ORIGINAL_SIZE + 65536;
    unsigned char *mem = (unsigned char *)_syscall6(
        SYS_MMAP_NR, 0, (long)alloc_sz,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

    if ((long)mem < 0 && (long)mem >= -4095) _syscall1(SYS_exit, 1);

    unsigned char *buf = mem;
    uint16_t *probs = (uint16_t *)(mem + PAYLOAD_ORIGINAL_SIZE);

    size_t src_size = PAYLOAD_COMPRESSED_SIZE;
    size_t dst_size = PAYLOAD_ORIGINAL_SIZE;
    if (decode_lzma(payload, &src_size, buf, &dst_size, probs) != 0) {
        _syscall1(SYS_exit, 1);
    }

#if PAYLOAD_USE_BCJ
    x86_decode(buf, dst_size);
#endif

    const char *fallback_path = NULL;
    int fd = (int)_syscall2(SYS_memfd_create, (long)"rt_run", 1);
    
    if (fd < 0) {
        fallback_path = "/tmp/.rt_rterm";
        fd = (int)_syscall3(SYS_open, (long)fallback_path, 0x242, 0700);
        if (fd < 0) {
            fallback_path = ".rt_run";
            fd = (int)_syscall3(SYS_open, (long)fallback_path, 0x242, 0700);
        }
        if (fd >= 0) {
            _syscall2(SYS_fchmod, (long)fd, 0700);
        }
    }

    if (fd < 0) _syscall1(SYS_exit, 1);

    size_t written = 0;
    while (written < dst_size) {
        long n = _syscall3(SYS_write, (long)fd, (long)(buf + written), (long)(dst_size - written));
        if (n <= 0) break;
        written += (size_t)n;
    }

    _syscall5(SYS_execveat, (long)fd, (long)"", (long)argv, (long)envp, 0x1000); 
    
    char path[64];
    path[0] = '/'; path[1] = 'p'; path[2] = 'r'; path[3] = 'o'; path[4] = 'c';
    path[5] = '/'; path[6] = 's'; path[7] = 'e'; path[8] = 'l'; path[9] = 'f';
    path[10] = '/'; path[11] = 'f'; path[12] = 'd'; path[13] = '/';
    format_fd_path(path, fd);
    _syscall3(SYS_execve, (long)path, (long)argv, (long)envp);

    if (fallback_path) {
        _syscall3(SYS_execve, (long)fallback_path, (long)argv, (long)envp);
        _syscall1(SYS_unlink, (long)fallback_path);
    }

    _syscall1(SYS_exit, 1);
    while(1);
}

#endif