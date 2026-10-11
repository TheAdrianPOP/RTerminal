#include "file_system.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#else
#  include <dirent.h>
#  include <unistd.h>
#endif

namespace FS {

// ── Path utilities ────────────────────────────────────────────────────────────

DynString join(const char* a, const char* b) {
    DynString r(a);
    if (!r.empty() && r[r.size() - 1] != '/' && r[r.size() - 1] != '\\') {
        r += '/';
    }
    r += b;
    return r;
}

DynString extension(const char* path) {
    if (!path) return DynString();
    const char* dot = strrchr(path, '.');
    return dot ? DynString(dot + 1) : DynString();
}

DynString stem(const char* path) {
    if (!path) return DynString();
    const char* s = strrchr(path, '/');
    if (!s) s = strrchr(path, '\\');
    const char* start = s ? s + 1 : path;
    const char* dot   = strrchr(start, '.');
    return dot ? DynString(start, (size_t)(dot - start)) : DynString(start);
}

DynString dirname(const char* path) {
    if (!path) return DynString(".");
    const char* s = strrchr(path, '/');
    if (!s) s = strrchr(path, '\\');
    return s ? DynString(path, (size_t)(s - path)) : DynString(".");
}

DynString filename(const char* path) {
    if (!path) return DynString();
    const char* s = strrchr(path, '/');
    if (!s) s = strrchr(path, '\\');
    return s ? DynString(s + 1) : DynString(path);
}

// ── Existence / metadata ──────────────────────────────────────────────────────

bool exists(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES);
#else
    return access(path, F_OK) == 0;
#endif
}

bool isFile(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
#endif
}

bool isDir(const char* path) {
    if (!path) return false;
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

size_t fileSize(const char* path) {
    if (!path) return 0;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER size;
        size.HighPart = fad.nFileSizeHigh;
        size.LowPart  = fad.nFileSizeLow;
        return static_cast<size_t>(size.QuadPart);
    }
    return 0;
#else
    struct stat st;
    return (stat(path, &st) == 0) ? static_cast<size_t>(st.st_size) : 0;
#endif
}

// ── Read / write helpers ──────────────────────────────────────────────────────

PodVector<uint8_t> readBytes(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return {}; }
    PodVector<uint8_t> buf(static_cast<size_t>(sz));
    fread(buf.data(), 1, static_cast<size_t>(sz), f);
    fclose(f);
    return buf;
}

DynString readText(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return {}; }
    DynString s;
    s.resize(static_cast<size_t>(sz));
    size_t read_bytes = fread(&s[0], 1, static_cast<size_t>(sz), f);
    fclose(f);
    s.resize(read_bytes);
    return s;
}

bool writeBytes(const char* path, const void* data, size_t len) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len;
}

bool writeText(const char* path, const char* text) {
    if (!text) return false;
    return writeBytes(path, text, strlen(text));
}

// ── Directory listing ─────────────────────────────────────────────────────────

PodVector<DirEntry> listDir(const char* path) {
    PodVector<DirEntry> entries;
#ifdef _WIN32
    DynString pat = join(path, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        entries.push_back({DynString(fd.cFileName), !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path);
    if (!d) return {};
    dirent* e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        DynString full = join(path, e->d_name);
        entries.push_back({DynString(e->d_name), isDir(full.c_str())});
    }
    closedir(d);
#endif
    return entries;
}

// ── Working directory ─────────────────────────────────────────────────────────

DynString cwd() {
    char buf[512] = {};
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(buf), buf);
#else
    if (!getcwd(buf, sizeof(buf))) buf[0] = '\0';
#endif
    return DynString(buf);
}

} // namespace FS