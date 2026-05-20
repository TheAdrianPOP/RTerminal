#pragma once
#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

// Lightweight file system utilities — path manipulation, existence checks,
// directory listing, and simple whole-file read/write helpers.
// No virtual FS; add zip mounting on top if needed.

#include "../core/dyn_string.h"
#include "../math/vector.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#  undef min
#  undef max
#else
#  include <dirent.h>
#  include <unistd.h>
#endif

namespace FS {

// ── Path utilities ────────────────────────────────────────────────────────────

inline DynString join(const char* a, const char* b) {
    DynString r(a);
    if (!r.empty() && r[r.size()-1] != '/' && r[r.size()-1] != '\\') r += '/';
    r += b;
    return r;
}
inline DynString extension(const char* path) {
    const char* dot = strrchr(path, '.');
    return dot ? DynString(dot+1) : DynString();
}
inline DynString stem(const char* path) {
    const char* s = strrchr(path, '/');  if (!s) s = strrchr(path, '\\');
    const char* start = s ? s+1 : path;
    const char* dot   = strrchr(start, '.');
    return dot ? DynString(start, (size_t)(dot-start)) : DynString(start);
}
inline DynString dirname(const char* path) {
    const char* s = strrchr(path, '/'); if (!s) s = strrchr(path, '\\');
    return s ? DynString(path, (size_t)(s-path)) : DynString(".");
}
inline DynString filename(const char* path) {
    const char* s = strrchr(path, '/'); if (!s) s = strrchr(path, '\\');
    return s ? DynString(s+1) : DynString(path);
}

// ── Existence / metadata ──────────────────────────────────────────────────────

inline bool exists(const char* path) {
    struct stat st; return stat(path, &st) == 0;
}
inline bool isFile(const char* path) {
    struct stat st; return stat(path, &st)==0 && (st.st_mode & S_IFREG);
}
inline bool isDir(const char* path) {
    struct stat st; return stat(path, &st)==0 && (st.st_mode & S_IFDIR);
}
inline size_t fileSize(const char* path) {
    struct stat st; return stat(path,&st)==0 ? (size_t)st.st_size : 0;
}

// ── Read / write helpers ──────────────────────────────────────────────────────

// Read entire file into a PodVector<uint8_t>. Returns empty on error.
inline PodVector<uint8_t> readBytes(const char* path) {
    FILE* f = fopen(path, "rb"); if (!f) return PodVector<uint8_t>{};
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    PodVector<uint8_t> buf((size_t)sz);
    fread(buf.data(),1,(size_t)sz,f); fclose(f);
    return buf;
}

// Read entire text file into a DynString. Returns empty on error.
inline DynString readText(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) return {};
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    DynString s; s.resize((size_t)sz);
    size_t read = fread(&s[0], 1, sz, f); fclose(f);
    s.resize(read); // may be smaller due to \r\n -> \n on Windows
    return s;
}

// Write bytes to file. Returns false on error.
inline bool writeBytes(const char* path, const void* data, size_t len) {
    FILE* f = fopen(path,"wb"); if (!f) return false;
    fwrite(data,1,len,f); fclose(f); return true;
}
inline bool writeText(const char* path, const char* text) {
    return writeBytes(path, text, strlen(text));
}

// ── Directory listing ─────────────────────────────────────────────────────────

struct DirEntry { DynString name; bool isDirectory; };

inline PodVector<DirEntry> listDir(const char* path) {
    PodVector<DirEntry> entries;
#ifdef _WIN32
    DynString pat = join(path, "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return PodVector<DirEntry>{};
    do {
        if (strcmp(fd.cFileName,".")==0||strcmp(fd.cFileName,"..")==0) continue;
        entries.push_back({DynString(fd.cFileName), !!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(path); if (!d) return PodVector<DirEntry>{};
    dirent* e;
    while ((e=readdir(d))) {
        if (strcmp(e->d_name,".")==0||strcmp(e->d_name,"..")==0) continue;
        DynString full = join(path, e->d_name);
        entries.push_back({DynString(e->d_name), isDir(full.c_str())});
    }
    closedir(d);
#endif
    return entries;
}

// ── Working directory ─────────────────────────────────────────────────────────

inline DynString cwd() {
    char buf[512] = {};
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(buf), buf);
#else
    getcwd(buf, sizeof(buf));
#endif
    return DynString(buf);
}

} // namespace FS

#endif
