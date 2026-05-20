#pragma once
#ifndef DATA_READER_H
#define DATA_READER_H

#include <cstdint>
#include <cstring>
#include "result.h"

enum class ReadErr : uint8_t {
    EndOfStream = 0,
};

class BinReader {
public:
    BinReader(const uint8_t* data, size_t size)
        : base_(data), size_(size), pos_(0) {}

    Result<uint8_t,  ReadErr> readU8()  { return readRaw<uint8_t>();  }
    Result<uint16_t, ReadErr> readU16() { return readRaw<uint16_t>(); }
    Result<uint32_t, ReadErr> readU32() { return readRaw<uint32_t>(); }
    Result<uint64_t, ReadErr> readU64() { return readRaw<uint64_t>(); }

    Result<int8_t,   ReadErr> readI8()  { return readRaw<int8_t>();   }
    Result<int16_t,  ReadErr> readI16() { return readRaw<int16_t>();  }
    Result<int32_t,  ReadErr> readI32() { return readRaw<int32_t>();  }
    Result<int64_t,  ReadErr> readI64() { return readRaw<int64_t>();  }

    Result<float,    ReadErr> readF32() { return readRaw<float>();    }
    Result<double,   ReadErr> readF64() { return readRaw<double>();   }

    Result<bool,     ReadErr> readBool() {
        auto r = readRaw<uint8_t>();
        if (r.isError()) return Result<bool, ReadErr>::Error(r.error());
        return Result<bool, ReadErr>::Ok(r.unwrap() != 0);
    }

    Result<void, ReadErr> readBytes(void* dst, size_t len) {
        if (pos_ + len > size_)
            return Result<void, ReadErr>::Error(ReadErr::EndOfStream);
        memcpy(dst, base_ + pos_, len);
        pos_ += len;
        return Result<void, ReadErr>::Ok();
    }

    Result<uint16_t, ReadErr> readStr(char* buf, size_t buf_cap) {
        auto lenRes = readU16();
        if (lenRes.isError()) return Result<uint16_t, ReadErr>::Error(lenRes.error());
        uint16_t stored = lenRes.unwrap();
        if (pos_ + stored > size_)
            return Result<uint16_t, ReadErr>::Error(ReadErr::EndOfStream);
        size_t copy = stored < buf_cap - sizeof(char) ? stored : buf_cap - sizeof(char);
        memcpy(buf, base_ + pos_, copy);
        buf[copy] = '\0';
        pos_ += stored;
        return Result<uint16_t, ReadErr>::Ok(stored);
    }

    size_t pos()       const { return pos_; }
    size_t remaining() const { return size_ - pos_; }
    bool   atEnd()     const { return pos_ >= size_; }

    void seek(size_t p) { pos_ = p < size_ ? p : size_; }
    void skip(size_t n) { pos_ = (pos_ + n < size_) ? pos_ + n : size_; }

private:
    const uint8_t* base_;
    size_t         size_;
    size_t         pos_;

    template<typename T>
    Result<T, ReadErr> readRaw() {
        if (pos_ + sizeof(T) > size_)
            return Result<T, ReadErr>::Error(ReadErr::EndOfStream);
        T v;
        memcpy(&v, base_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return Result<T, ReadErr>::Ok(static_cast<T&&>(v));
    }
};

#endif