#pragma once
#ifndef STR_H
#define STR_H

#include "internal.h"
#include "runtime.h"
#include "console.h"

template<int N>
struct StrBuf {
    static_assert(N > 1, "StrBuf capacity must be at least 2");

    char data[N];
    int  len = 0;

    StrBuf() { data[0] = '\0'; }

    StrBuf(const char* s) : len(0) {
        if (s) while (len < N - 1 && s[len]) { data[len] = s[len]; ++len; }
        data[len] = '\0';
    }

    template<int M>
    StrBuf(const StrBuf<M>& o) : len(0) {
        while (len < N - 1 && len < o.len) { data[len] = o.data[len]; ++len; }
        data[len] = '\0';
    }

    StrBuf& operator=(const char* s) {
        len = 0;
        if (s) {
            while (len < N - 1 && s[len]) { data[len] = s[len]; ++len; }
        }
        data[len] = '\0';
        return *this;
    }

    template<int M>
    StrBuf& operator=(const StrBuf<M>& o) {
        len = 0;
        while (len < N - 1 && len < o.len) { data[len] = o.data[len]; ++len; }
        data[len] = '\0';
        return *this;
    }

    const char* c_str()  const { return data; }
    int         length() const { return len; }
    int         size()   const { return len; }
    bool        empty()  const { return len == 0; }
    void        clear()        { len = 0; data[0] = '\0'; }

    char&       operator[](int i)       { assert(i >= 0 && i < N); return data[i]; }
    const char& operator[](int i) const { assert(i >= 0 && i < N); return data[i]; }

    char*       begin()       { return data; }
    char*       end()         { return data + len; }
    const char* begin() const { return data; }
    const char* end()   const { return data + len; }

    bool operator==(const char* s)    const { return s && strcmp(data, s) == 0; }
    bool operator!=(const char* s)    const { return !(*this == s); }

    bool equalsIgnoreCase(const char* s) const { return s && rt_stricmp(data, s) == 0; }

    template<int M>
    bool operator==(const StrBuf<M>& o) const {
        return len == o.len && memcmp(data, o.data, (size_t)len) == 0;
    }
    template<int M>
    bool operator!=(const StrBuf<M>& o) const { return !(*this == o); }

    StrBuf& operator+=(char c) {
        if (len < N - 1) { data[len++] = c; data[len] = '\0'; }
        return *this;
    }

    StrBuf& operator+=(const char* s) {
        if (s) {
            while (*s && len < N - 1) data[len++] = *s++;
        }
        data[len] = '\0';
        return *this;
    }

    template<int M>
    StrBuf& operator+=(const StrBuf<M>& o) { return *this += o.data; }

    bool startsWith(const char* s) const { return s && strncmp(data, s, strlen(s)) == 0; }
    bool endsWith  (const char* s) const {
        if (!s) return false;
        int sl = (int)strlen(s); return len >= sl && strcmp(data + len - sl, s) == 0;
    }

    void toLower() {
        for (int i = 0; i < len; ++i) data[i] = (char)rt_tolower((unsigned char)data[i]);
    }
    void toUpper() {
        for (int i = 0; i < len; ++i) data[i] = (char)rt_toupper((unsigned char)data[i]);
    }

    void trim() {
        if (len == 0) return;
        int start = 0, end = len;
        while (start < end && rt_isspace((unsigned char)data[start])) start++;
        while (end > start && rt_isspace((unsigned char)data[end - 1])) end--;
        if (start > 0 || end < len) {
            len = end - start;
            memmove(data, data + start, (size_t)len);
            data[len] = '\0';
        }
    }

    void insert(int pos, int count, char ch) {
        if (pos < 0 || pos > len || count <= 0) return;
        int space = N - 1 - len;
        int actual = (count < space) ? count : space;
        if (actual <= 0) return;
        memmove(data + pos + actual, data + pos, (size_t)(len - pos + 1));
        memset(data + pos, ch, (size_t)actual);
        len += actual;
    }

    void erase(int pos, int count) {
        if (pos < 0 || pos >= len || count <= 0) return;
        int actual = (pos + count > len) ? len - pos : count;
        memmove(data + pos, data + pos + actual, (size_t)(len - pos - actual + 1));
        len -= actual;
    }

    static StrBuf format(const char* fmt, ...) {
        StrBuf res;
        va_list ap; va_start(ap, fmt);
        int r = rt_vsnprintf(res.data, N, fmt, ap);
        va_end(ap);
        res.len = (r < 0 || r >= N) ? N - 1 : r;
        res.data[res.len] = '\0';
        return res;
    }
};

template<typename T>
struct OwnedArr {
    T*     p    = nullptr;
    size_t cap  = 0;

    OwnedArr() = default;
    explicit OwnedArr(size_t n) : p(new T[n]()), cap(n) {}
    ~OwnedArr() { delete[] p; }

    OwnedArr(OwnedArr&& o) noexcept : p(o.p), cap(o.cap) { o.p = nullptr; o.cap = 0; }
    OwnedArr& operator=(OwnedArr&& o) noexcept {
        if (this != &o) { delete[] p; p = o.p; cap = o.cap; o.p = nullptr; o.cap = 0; }
        return *this;
    }
    OwnedArr(const OwnedArr&)            = delete;
    OwnedArr& operator=(const OwnedArr&) = delete;

    void reset(size_t n) {
        delete[] p;
        p   = new T[n]();
        cap = n;
    }

    T*       get()              { return p; }
    const T* get()        const { return p; }
    T&       operator[](size_t i)       { assert(p && i < cap); return p[i]; }
    const T& operator[](size_t i) const { assert(p && i < cap); return p[i]; }
    explicit operator bool() const { return p != nullptr; }
};

template<typename T>
struct OwnedSingle {
    T* p = nullptr;

    OwnedSingle() = default;
    explicit OwnedSingle(T* ptr) : p(ptr) {}
    ~OwnedSingle() { delete p; }

    OwnedSingle(OwnedSingle&& o) noexcept : p(o.p) { o.p = nullptr; }
    OwnedSingle& operator=(OwnedSingle&& o) noexcept {
        if (this != &o) { delete p; p = o.p; o.p = nullptr; }
        return *this;
    }
    OwnedSingle(const OwnedSingle&)            = delete;
    OwnedSingle& operator=(const OwnedSingle&) = delete;

    T*       get()              { assert(p); return p; }
    const T* get()        const { assert(p); return p; }
    T*       operator->()       { assert(p); return p; }
    const T* operator->() const { assert(p); return p; }
    T&       operator*()        { assert(p); return *p; }
    const T& operator*()  const { assert(p); return *p; }
    explicit operator bool() const { return p != nullptr; }

    void reset(T* np = nullptr) { delete p; p = np; }
};

struct MsgCallback {
    void (*fn)(const char* msg, RGBColor col, void* ctx) = nullptr;
    void* ctx = nullptr;

    MsgCallback() = default;
    MsgCallback(void (*f)(const char*, RGBColor, void*), void* c) : fn(f), ctx(c) {}

    void operator()(const char* msg, RGBColor col) const {
        if (fn) fn(msg, col, ctx);
    }
    explicit operator bool() const { return fn != nullptr; }
};

#endif // STR_H