#pragma once

#include "../math/vector.h"
#include "../platform/console.h"
#include "../core/str.h"

struct Texture {
    int      width  = 0;
    int      height = 0;
    uint8_t* pixels = nullptr;
    StrBuf<256> name;

    Texture() = default;
    ~Texture() { delete[] pixels; }

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;
    static Texture* load(const char* filename, MsgCallback cb = MsgCallback{});

    int  getWidth()  const { return width;  }
    int  getHeight() const { return height; }

    Vec3 getColor(float u, float v) const;
    bool getCharacterAndColor(float u, float v,
                              char& out_char, Vec3& out_base_color) const;
};