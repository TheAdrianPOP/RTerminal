#pragma once
#ifndef DATA_WRITER_H
#define DATA_WRITER_H

#include <cstdint>
#include <cstring>
#include <cassert>
#include "engine_config.h"
#include "str.h"
class BinWriter {
public:
    explicit BinWriter(size_t initial_cap = kBinWriterInitialCap)
        : buf_(initial_cap), used_(0) {}

    void writeU8 (uint8_t  v) { append(&v, sizeof(v)); }
    void writeU16(uint16_t v) { append(&v, sizeof(v)); }
    void writeU32(uint32_t v) { append(&v, sizeof(v)); }
    void writeU64(uint64_t v) { append(&v, sizeof(v)); }

    void writeI8 (int8_t  v) { append(&v, sizeof(v)); }
    void writeI16(int16_t v) { append(&v, sizeof(v)); }
    void writeI32(int32_t v) { append(&v, sizeof(v)); }
    void writeI64(int64_t v) { append(&v, sizeof(v)); }

    void writeF32(float  v)  { append(&v, sizeof(v)); }
    void writeF64(double v)  { append(&v, sizeof(v)); }

    void writeBool(bool v)   { uint8_t b = static_cast<uint8_t>(v); append(&b, sizeof(b)); }

    void writeBytes(const void* src, size_t len) {
        if (!src || !len) return;
        append(src, len);
    }
    void writeStr(const char* s, uint16_t len) {
        writeU16(len);
        if (len) append(s, len);
    }
    void writeStr(const char* s) {
        if (!s) { writeU16(0); return; }
        size_t n = 0;
        while (s[n]) ++n;
        assert(n <= kBinWriterMaxStrBytes && "BinWriter: string too long for writeStr");
        writeStr(s, static_cast<uint16_t>(n));
    }

    const uint8_t* data() const { return buf_.get(); }
    size_t         size() const { return used_; }
    bool           empty()const { return used_ == 0; }

    void reset() { used_ = 0; }

private:
    OwnedArr<uint8_t> buf_;
    size_t            used_;

    void reserve(size_t needed) {
        size_t cap = buf_.cap ? buf_.cap : kBinWriterMinCap;
        while (cap < needed) cap *= kBinWriterGrowthFactor;
        if (cap <= buf_.cap) return;

        OwnedArr<uint8_t> next(cap);
        if (used_) memcpy(next.get(), buf_.get(), used_);
        buf_ = static_cast<OwnedArr<uint8_t>&&>(next);
    }

    void append(const void* src, size_t len) {
        reserve(used_ + len);
        memcpy(buf_.get() + used_, src, len);
        used_ += len;
    }
};

#endif
