#pragma once
#ifndef DYN_STRING_H
#define DYN_STRING_H

#include "runtime.h"
#include "math_utils.h"
#include "move.h"
#include "internal.h"

class DynString {
    static constexpr size_t kSSO = 22; // local buffer size (+ 1 null terminator)
public:
    DynString();
    DynString(const char* s);
    DynString(const char* s, size_t n);
    DynString(const DynString& o);
    DynString(DynString&& o) noexcept;
    ~DynString();

    DynString& operator=(const char* s);
    DynString& operator=(const DynString& o);
    DynString& operator=(DynString&& o) noexcept;

    void assign(const char* s, size_t n);

    inline const char* c_str()  const { return isSSO() ? sso_.buf : heap_.ptr; }
    inline size_t      size()   const { return size_; }
    inline bool        empty()  const { return size_ == 0; }

    char  operator[](size_t i) const { assert(i < size_); return c_str()[i]; }
    char& operator[](size_t i)       { assert(i < size_); return mutableData()[i]; }

    void reserve(size_t cap);
    void append(const char* s, size_t n);
    void append(const char* s)        { append(s, s ? strlen(s) : 0); }
    void append(char c)               { append(&c, 1); }
    void append(const DynString& o)   { append(o.c_str(), o.size_); }

    DynString& operator+=(const char* s)      { append(s); return *this; }
    DynString& operator+=(const DynString& o) { append(o.c_str(), o.size_); return *this; }
    DynString& operator+=(char c)             { append(&c, 1); return *this; }

    DynString operator+(const DynString& o) const { DynString r(*this); r.append(o); return r; }
    DynString operator+(const char* s)      const { DynString r(*this); r.append(s); return r; }

    bool operator==(const char* s)      const { return s && strcmp(c_str(), s) == 0; }
    bool operator==(const DynString& o) const { return size_ == o.size_ && strcmp(c_str(), o.c_str()) == 0; }
    bool operator!=(const char* s)      const { return !(*this == s); }
    bool operator!=(const DynString& o) const { return !(*this == o); }
    bool operator< (const DynString& o) const { return strcmp(c_str(), o.c_str()) < 0; }

    bool equalsIgnoreCase(const char* s)      const { return rt_stricmp(c_str(), s) == 0; }
    bool equalsIgnoreCase(const DynString& o) const { return rt_stricmp(c_str(), o.c_str()) == 0; }

    void clear() {
        size_ = 0;
        mutableData()[0] = '\0';
    }

    void resize(size_t n, char fill = ' ');

    DynString substr(size_t pos, size_t len = (size_t)-1) const;
    size_t    find(char c, size_t from = 0) const;
    size_t    find(const char* s, size_t from = 0) const;

    bool startsWith(const char* s) const { return s && strncmp(c_str(), s, strlen(s)) == 0; }
    bool endsWith(const char* s)   const {
        if (!s) return false;
        size_t sl = strlen(s);
        return size_ >= sl && strcmp(c_str() + size_ - sl, s) == 0;
    }
    bool contains(const char* s) const { return find(s) != npos; }

    void toLower();
    void toUpper();
    void trim();

    static constexpr size_t npos = (size_t)-1;
    static DynString format(const char* fmt, ...);

private:
    struct HeapData { char* ptr; size_t cap; };
    struct SSOData  { char  buf[kSSO + 1]; };

    union {
        HeapData heap_;
        SSOData  sso_;
    };
    size_t  size_;
    uint8_t ssoFlag_; // 1 = using SSO, 0 = heap

    inline bool  isSSO()       const { return ssoFlag_ != 0; }
    inline char* mutableData()       { return ssoFlag_ ? sso_.buf : heap_.ptr; }

    void init(const char* s, size_t n);
    void moveFrom(DynString& o);
};

#endif // DYN_STRING_H