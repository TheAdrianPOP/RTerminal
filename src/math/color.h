#pragma once
#ifndef COLOR_H
#define COLOR_H

#include "math_utils.h"
#include "console.h"
#include <cstdint>
#include <cstring>

struct Color {
    uint8_t r, g, b, a;

    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
    constexpr explicit Color(uint32_t rgba) : r((rgba>>24)&0xFF), g((rgba>>16)&0xFF), b((rgba>>8)&0xFF), a(rgba&0xFF) {}

    uint32_t toU32()     const { return ((uint32_t)r<<24)|((uint32_t)g<<16)|((uint32_t)b<<8)|a; }
    RGBColor toRGBColor()const { return RGBColor(r, g, b); }

    Color blendOver(Color dst) const {
        if (a == 0)   return dst;
        if (a == 255) return *this;
        uint32_t ia = 255 - a;
        return Color(
            (uint8_t)((r * a + dst.r * ia) / 255),
            (uint8_t)((g * a + dst.g * ia) / 255),
            (uint8_t)((b * a + dst.b * ia) / 255),
            (uint8_t)(a + dst.a * ia / 255)
        );
    }

    Color lerp(Color other, float t) const {
        t = fast_clamp(t, 0.f, 1.f);
        return Color(
            (uint8_t)fast_lerp((float)r, (float)other.r, t),
            (uint8_t)fast_lerp((float)g, (float)other.g, t),
            (uint8_t)fast_lerp((float)b, (float)other.b, t),
            (uint8_t)fast_lerp((float)a, (float)other.a, t)
        );
    }

    Color withAlpha(uint8_t na) const { return Color(r, g, b, na); }

    bool operator==(Color o) const { return toU32()==o.toU32(); }
    bool operator!=(Color o) const { return toU32()!=o.toU32(); }
};

struct ColorHSV { float h, s, v, a; };

inline Color hsvToRgb(float h, float s, float v, float a = 1.0f) {
    h = h - (float)(int)h;
    if (h < 0.f) h += 1.f;
    float c = v * s;
    float x = c * (1.f - fast_abs(h * 6.f - (float)(int)(h * 6.f) * 2.f - 1.f));
    float m = v - c;
    float r=m, g=m, b=m;
    int   sector = (int)(h * 6.f);
    switch (sector % 6) {
        case 0: r+=c; g+=x; break;
        case 1: r+=x; g+=c; break;
        case 2: g+=c; b+=x; break;
        case 3: g+=x; b+=c; break;
        case 4: r+=x; b+=c; break;
        case 5: r+=c; b+=x; break;
    }
    return Color((uint8_t)(r*255),(uint8_t)(g*255),(uint8_t)(b*255),(uint8_t)(a*255));
}

inline ColorHSV rgbToHsv(Color c) {
    float r = c.r/255.f, g = c.g/255.f, b = c.b/255.f;
    float mx = fast_max(r, fast_max(g, b));
    float mn = fast_min(r, fast_min(g, b));
    float d  = mx - mn;
    float h  = 0.f, s = (mx < 1e-5f ? 0.f : d/mx), v = mx;
    if (d > 1e-5f) {
        if      (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
        else if (mx == g) h = (b - r) / d + 2.f;
        else              h = (r - g) / d + 4.f;
        h /= 6.f;
    }
    return {h, s, v, c.a/255.f};
}

namespace Palette {
    constexpr Color Black      {0,   0,   0  };
    constexpr Color White      {255, 255, 255};
    constexpr Color Red        {255, 0,   0  };
    constexpr Color Green      {0,   200, 0  };
    constexpr Color Blue       {0,   80,  255};
    constexpr Color Yellow     {255, 220, 0  };
    constexpr Color Cyan       {0,   220, 220};
    constexpr Color Magenta    {220, 0,   220};
    constexpr Color Orange     {255, 140, 0  };
    constexpr Color Gray       {128, 128, 128};
    constexpr Color DarkGray   {64,  64,  64 };
    constexpr Color Transparent{0,   0,   0, 0};
}

#endif // COLOR_H