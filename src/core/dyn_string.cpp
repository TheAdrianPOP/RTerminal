#include "dyn_string.h"

DynString::DynString() {
    init("", 0);
}

DynString::DynString(const char* s) {
    init(s, s ? strlen(s) : 0);
}

DynString::DynString(const char* s, size_t n) {
    init(s, n);
}

DynString::DynString(const DynString& o) {
    init(o.c_str(), o.size_);
}

DynString::DynString(DynString&& o) noexcept {
    moveFrom(o);
}

DynString::~DynString() {
    if (!isSSO()) {
        delete[] heap_.ptr;
    }
}

DynString& DynString::operator=(const char* s) {
    assign(s, s ? strlen(s) : 0);
    return *this;
}

DynString& DynString::operator=(const DynString& o) {
    if (this != &o) {
        assign(o.c_str(), o.size_);
    }
    return *this;
}

DynString& DynString::operator=(DynString&& o) noexcept {
    if (this != &o) {
        if (!isSSO()) {
            delete[] heap_.ptr;
        }
        moveFrom(o);
    }
    return *this;
}

void DynString::init(const char* s, size_t n) {
    size_ = n;
    char* dst;
    if (n <= kSSO) {
        ssoFlag_ = 1;
        dst = sso_.buf;
    } else {
        ssoFlag_ = 0;
        heap_.cap = n + 1;
        dst = heap_.ptr = new char[heap_.cap];
    }
    if (s && n > 0) {
        memcpy(dst, s, n);
    }
    dst[n] = '\0';
}

void DynString::moveFrom(DynString& o) {
    ssoFlag_ = o.ssoFlag_;
    size_    = o.size_;
    if (o.isSSO()) {
        memcpy(sso_.buf, o.sso_.buf, o.size_ + 1);
    } else {
        heap_ = o.heap_;
        o.ssoFlag_ = 1;
        o.sso_.buf[0] = '\0';
        o.size_ = 0;
    }
}

void DynString::assign(const char* s, size_t n) {
    if (!s || n == 0) {
        clear();
        return;
    }

    if (n <= kSSO) {
        char tmp[kSSO + 1];
        memcpy(tmp, s, n);
        tmp[n] = '\0';

        if (!isSSO()) {
            delete[] heap_.ptr;
        }
        ssoFlag_ = 1;
        size_ = n;
        memcpy(sso_.buf, tmp, n + 1);
    } else {
        if (!isSSO() && heap_.cap >= n + 1) {
            memmove(heap_.ptr, s, n);
            heap_.ptr[n] = '\0';
            size_ = n;
        } else {
            size_t new_cap = n + 1;
            char* new_ptr = new char[new_cap];
            memcpy(new_ptr, s, n);
            new_ptr[n] = '\0';

            if (!isSSO()) {
                delete[] heap_.ptr;
            }
            ssoFlag_  = 0;
            heap_.ptr = new_ptr;
            heap_.cap = new_cap;
            size_     = n;
        }
    }
}

void DynString::reserve(size_t cap) {
    size_t current_cap = isSSO() ? kSSO : (heap_.cap - 1);
    if (cap <= current_cap) return;

    size_t new_cap = cap + 1;
    char* new_ptr = new char[new_cap];
    memcpy(new_ptr, c_str(), size_ + 1);

    if (!isSSO()) {
        delete[] heap_.ptr;
    }
    ssoFlag_  = 0;
    heap_.ptr = new_ptr;
    heap_.cap = new_cap;
}

void DynString::append(const char* s, size_t n) {
    if (!s || n == 0) return;
    size_t new_len = size_ + n;
    size_t current_cap = isSSO() ? kSSO : (heap_.cap - 1);

    if (new_len <= current_cap) {
        memmove(mutableData() + size_, s, n);
        size_ = new_len;
        mutableData()[size_] = '\0';
    } else {
        size_t next_cap = (new_len * 2 + 1 < 32) ? 32 : (new_len * 2 + 1);
        char* new_ptr = new char[next_cap];

        memcpy(new_ptr, c_str(), size_);
        memcpy(new_ptr + size_, s, n);
        new_ptr[new_len] = '\0';

        if (!isSSO()) {
            delete[] heap_.ptr;
        }
        ssoFlag_  = 0;
        heap_.ptr = new_ptr;
        heap_.cap = next_cap;
        size_     = new_len;
    }
}

void DynString::resize(size_t n, char fill) {
    if (n > size_) {
        reserve(n);
        memset(mutableData() + size_, fill, n - size_);
    }
    size_ = n;
    mutableData()[size_] = '\0';
}

void DynString::toLower() {
    char* p = mutableData();
    for (size_t i = 0; i < size_; ++i) {
        p[i] = static_cast<char>(rt_tolower(static_cast<unsigned char>(p[i])));
    }
}

void DynString::toUpper() {
    char* p = mutableData();
    for (size_t i = 0; i < size_; ++i) {
        p[i] = static_cast<char>(rt_toupper(static_cast<unsigned char>(p[i])));
    }
}

void DynString::trim() {
    if (size_ == 0) return;
    char* p = mutableData();
    size_t start = 0;
    size_t end = size_;

    while (start < end && rt_isspace(static_cast<unsigned char>(p[start]))) start++;
    while (end > start && rt_isspace(static_cast<unsigned char>(p[end - 1]))) end--;

    if (start > 0 || end < size_) {
        size_ = end - start;
        memmove(p, p + start, size_);
        p[size_] = '\0';
    }
}

DynString DynString::substr(size_t pos, size_t len) const {
    if (pos >= size_) return {};
    if (len == (size_t)-1 || pos + len > size_) len = size_ - pos;
    return DynString(c_str() + pos, len);
}

size_t DynString::find(char c, size_t from) const {
    if (from >= size_) return npos;
    const char* p = strchr(c_str() + from, c);
    return p ? static_cast<size_t>(p - c_str()) : npos;
}

size_t DynString::find(const char* s, size_t from) const {
    if (from >= size_ || !s) return npos;
    const char* p = strstr(c_str() + from, s);
    return p ? static_cast<size_t>(p - c_str()) : npos;
}

DynString DynString::format(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (r >= 0 && static_cast<size_t>(r) < sizeof(buf)) {
        return DynString(buf, static_cast<size_t>(r));
    }

    if (r > 0) {
        size_t needed = static_cast<size_t>(r) + 1;
        char* heap_buf = new char[needed];
        va_start(ap, fmt);
        vsnprintf(heap_buf, needed, fmt, ap);
        va_end(ap);
        DynString res(heap_buf, static_cast<size_t>(r));
        delete[] heap_buf;
        return res;
    }

    return DynString();
}