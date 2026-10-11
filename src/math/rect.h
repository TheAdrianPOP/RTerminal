#pragma once
#ifndef RECT_H
#define RECT_H

#include "vector.h"

struct Rect {
    float x, y, w, h;

    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}
    static Rect fromMinMax(Vec2 mn, Vec2 mx) { return {mn.x, mn.y, mx.x-mn.x, mx.y-mn.y}; }
    static Rect fromCenter(Vec2 c, Vec2 half) { return {c.x-half.x, c.y-half.y, half.x*2, half.y*2}; }

    FORCE_INLINE Vec2 min()    const { return {x, y}; }
    FORCE_INLINE Vec2 max()    const { return {x+w, y+h}; }
    FORCE_INLINE Vec2 center() const { return {x+w*0.5f, y+h*0.5f}; }
    FORCE_INLINE Vec2 size()   const { return {w, h}; }
    FORCE_INLINE float left()  const { return x; }
    FORCE_INLINE float right() const { return x+w; }
    FORCE_INLINE float top()   const { return y; }
    FORCE_INLINE float bottom()const { return y+h; }
    FORCE_INLINE float area()  const { return w*h; }
    FORCE_INLINE bool  empty() const { return w<=0||h<=0; }

    FORCE_INLINE bool contains(Vec2 p) const {
        return p.x>=x && p.x<x+w && p.y>=y && p.y<y+h;
    }
    FORCE_INLINE bool contains(Vec2i p) const {
        return contains(p.toFloat());
    }
    FORCE_INLINE bool overlaps(const Rect& o) const {
        return x<o.x+o.w && x+w>o.x && y<o.y+o.h && y+h>o.y;
    }
    FORCE_INLINE Rect intersect(const Rect& o) const {
        float lx = fast_max(x, o.x), ly = fast_max(y, o.y);
        float rx = fast_min(x+w, o.x+o.w), ry = fast_min(y+h, o.y+o.h);
        if (rx<lx||ry<ly) return {};
        return {lx, ly, rx-lx, ry-ly};
    }
    FORCE_INLINE Rect expand(float n)    const { return {x-n, y-n, w+n*2, h+n*2}; }
    FORCE_INLINE Rect shrink(float n)    const { return expand(-n); }
    FORCE_INLINE Rect translate(Vec2 d)  const { return {x+d.x, y+d.y, w, h}; }
    FORCE_INLINE Rect scale(float s)     const { return {x*s, y*s, w*s, h*s}; }

    FORCE_INLINE Rect splitLeft(float px)  const { return {x,    y, px,   h}; }
    FORCE_INLINE Rect splitRight(float px) const { return {x+px, y, w-px, h}; }
    FORCE_INLINE Rect splitTop(float py)   const { return {x, y,    w, py};   }
    FORCE_INLINE Rect splitBottom(float py)const { return {x, y+py, w, h-py}; }

    bool operator==(const Rect& o) const { return x==o.x&&y==o.y&&w==o.w&&h==o.h; }
    bool operator!=(const Rect& o) const { return !(*this==o); }
};

struct Recti {
    int x, y, w, h;

    Recti() : x(0), y(0), w(0), h(0) {}
    Recti(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}
    explicit Recti(Rect r) : x((int)r.x), y((int)r.y), w((int)r.w), h((int)r.h) {}
    static Recti fromMinMax(Vec2i mn, Vec2i mx) { return {mn.x, mn.y, mx.x-mn.x, mx.y-mn.y}; }

    FORCE_INLINE Vec2i  min()     const { return {x, y}; }
    FORCE_INLINE Vec2i  max()     const { return {x+w, y+h}; }
    FORCE_INLINE int    left()    const { return x; }
    FORCE_INLINE int    right()   const { return x+w; }
    FORCE_INLINE int    top()     const { return y; }
    FORCE_INLINE int    bottom()  const { return y+h; }
    FORCE_INLINE bool   empty()   const { return w<=0||h<=0; }
    FORCE_INLINE Rect   toFloat() const { return {(float)x,(float)y,(float)w,(float)h}; }

    FORCE_INLINE bool contains(Vec2i p) const {
        return p.x>=x && p.x<x+w && p.y>=y && p.y<y+h;
    }
    FORCE_INLINE bool overlaps(const Recti& o) const {
        return x<o.x+o.w && x+w>o.x && y<o.y+o.h && y+h>o.y;
    }
    FORCE_INLINE Recti intersect(const Recti& o) const {
        int lx=x>o.x?x:o.x, ly=y>o.y?y:o.y;
        int rx=(x+w<o.x+o.w?x+w:o.x+o.w), ry=(y+h<o.y+o.h?y+h:o.y+o.h);
        if (rx<lx||ry<ly) return {};
        return {lx,ly,rx-lx,ry-ly};
    }
    FORCE_INLINE Recti expand(int n)    const { return {x-n,y-n,w+n*2,h+n*2}; }
    FORCE_INLINE Recti translate(Vec2i d) const { return {x+d.x,y+d.y,w,h}; }

    bool operator==(const Recti& o) const { return x==o.x&&y==o.y&&w==o.w&&h==o.h; }
    bool operator!=(const Recti& o) const { return !(*this==o); }
};

#endif // RECT_H