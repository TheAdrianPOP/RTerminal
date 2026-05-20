#pragma once
#ifndef DYN_STRING_H
#define DYN_STRING_H

// Heap-allocated dynamic string with small-string optimisation (SSO ≤ 22 chars).
// Compatible with StrBuf<N> — can be used alongside the fixed-size string in str.h.

#include "math_utils.h"
#include "arena.h"
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cassert>

class DynString {
    static constexpr size_t kSSO = 22; // local buffer size (+ 1 null)
public:
    DynString()                         { initSSO("", 0); }
    DynString(const char* s)            { size_t n=strlen(s); init(s, n); }
    DynString(const char* s, size_t n)  { init(s, n); }
    DynString(const DynString& o)       { init(o.c_str(), o.size_); }
    DynString(DynString&& o) noexcept   { moveFrom(o); }
    ~DynString()                        { if (!isSSO()) delete[] heap_.ptr; }

    DynString& operator=(const char* s)      { assign(s, strlen(s)); return *this; }
    DynString& operator=(const DynString& o) { if (this!=&o) assign(o.c_str(), o.size_); return *this; }
    DynString& operator=(DynString&& o) noexcept {
        if (this!=&o) { if(!isSSO()) delete[] heap_.ptr; moveFrom(o); }
        return *this;
    }

    void assign(const char* s, size_t n) {
        if (!isSSO()) delete[] heap_.ptr;
        init(s, n);
    }

    const char* c_str()  const { return isSSO() ? sso_.buf : heap_.ptr; }
    size_t      size()   const { return size_; }
    bool        empty()  const { return size_ == 0; }

    char  operator[](size_t i) const { assert(i<size_); return c_str()[i]; }
    char& operator[](size_t i)       { assert(i<size_); return mutableData()[i]; }

    void append(const char* s, size_t n) {
        size_t newLen = size_ + n;
        grow(newLen);
        memcpy(mutableData() + size_, s, n);
        size_ = newLen;
        mutableData()[size_] = '\0';
    }
    void append(const char* s) { append(s, strlen(s)); }
    void append(char c)        { append(&c, 1); }
    void append(const DynString& o) { append(o.c_str(), o.size_); }

    DynString& operator+=(const char* s)      { append(s); return *this; }
    DynString& operator+=(const DynString& o) { append(o); return *this; }
    DynString& operator+=(char c)             { append(c); return *this; }

    DynString operator+(const DynString& o) const { DynString r(*this); r.append(o); return r; }

    bool operator==(const char* s)      const { return strcmp(c_str(), s)==0; }
    bool operator==(const DynString& o) const { return size_==o.size_ && strcmp(c_str(),o.c_str())==0; }
    bool operator!=(const char* s)      const { return !(*this==s); }
    bool operator!=(const DynString& o) const { return !(*this==o); }
    bool operator< (const DynString& o) const { return strcmp(c_str(),o.c_str())<0; }

    void clear() { size_=0; mutableData()[0]='\0'; }
    void resize(size_t n, char fill=' ') {
        if (n > size_) { grow(n); memset(mutableData()+size_, fill, n-size_); }
        size_=n; mutableData()[size_]='\0';
    }

    // Substring (copies)
    DynString substr(size_t pos, size_t len=(size_t)-1) const {
        if (pos>=size_) return {};
        if (len==(size_t)-1||pos+len>size_) len=size_-pos;
        return DynString(c_str()+pos, len);
    }

    size_t find(char c, size_t from=0) const {
        const char* p = strchr(c_str()+from, c);
        return p ? (size_t)(p-c_str()) : npos;
    }
    size_t find(const char* s, size_t from=0) const {
        const char* p = strstr(c_str()+from, s);
        return p ? (size_t)(p-c_str()) : npos;
    }
    bool startsWith(const char* s) const { return strncmp(c_str(),s,strlen(s))==0; }
    bool endsWith  (const char* s) const {
        size_t sl=strlen(s); return size_>=sl && strcmp(c_str()+size_-sl,s)==0;
    }
    bool contains(const char* s) const { return find(s) != npos; }

    static constexpr size_t npos = (size_t)-1;

    // printf-style formatting — returns a DynString
    static DynString format(const char* fmt, ...) {
        char buf[512]; va_list ap; va_start(ap,fmt);
        vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap);
        return DynString(buf);
    }

private:
    struct HeapData { char* ptr; size_t cap; };
    struct SSOData  { char  buf[kSSO+1]; };

    union { HeapData heap_; SSOData sso_; };
    size_t   size_;
    uint8_t  ssoFlag_; // 1 = using SSO

    bool isSSO() const { return ssoFlag_ != 0; }

    void initSSO(const char* s, size_t n) {
        ssoFlag_=1; size_=n;
        memcpy(sso_.buf, s, n); sso_.buf[n]='\0';
    }
    void init(const char* s, size_t n) {
        if (n <= kSSO) { initSSO(s,n); return; }
        ssoFlag_=0; size_=n;
        heap_.cap = n+1; heap_.ptr = new char[heap_.cap];
        memcpy(heap_.ptr, s, n); heap_.ptr[n]='\0';
    }
    void moveFrom(DynString& o) {
        ssoFlag_=o.ssoFlag_; size_=o.size_;
        if (o.isSSO()) memcpy(sso_.buf, o.sso_.buf, o.size_+1);
        else           { heap_=o.heap_; o.heap_.ptr=nullptr; o.ssoFlag_=1; o.size_=0; }
    }
    char* mutableData() { return isSSO() ? sso_.buf : heap_.ptr; }

    void grow(size_t need) {
        if (isSSO()) {
            if (need <= kSSO) return;
            // Transition from SSO to heap
            size_t nc = need*2+1;
            char* p = new char[nc];
            memcpy(p, sso_.buf, size_+1);
            ssoFlag_=0; heap_.ptr=p; heap_.cap=nc;
        } else {
            if (need < heap_.cap) return;
            size_t nc = need*2+1;
            char* p = new char[nc];
            memcpy(p, heap_.ptr, size_+1);
            delete[] heap_.ptr; heap_.ptr=p; heap_.cap=nc;
        }
    }
};

#endif
