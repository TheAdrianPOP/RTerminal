#pragma once
#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include "../core/dyn_string.h"
#include "../math/vector.h"
#include <cstdint>
#include <cstddef>

namespace FS {

struct DirEntry {
    DynString name;
    bool isDirectory;
};

// Path utilities
DynString join(const char* a, const char* b);
DynString extension(const char* path);
DynString stem(const char* path);
DynString dirname(const char* path);
DynString filename(const char* path);

// Existence / metadata
bool exists(const char* path);
bool isFile(const char* path);
bool isDir(const char* path);
size_t fileSize(const char* path);

// Read / write helpers
PodVector<uint8_t> readBytes(const char* path);
DynString          readText(const char* path);
bool writeBytes(const char* path, const void* data, size_t len);
bool writeText(const char* path, const char* text);

// Directory listing
PodVector<DirEntry> listDir(const char* path);

// Working directory
DynString cwd();

} // namespace FS

#endif // FILE_SYSTEM_H