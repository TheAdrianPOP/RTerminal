#pragma once
#ifndef DRAW2D_H
#define DRAW2D_H

// 2D drawing primitives that write directly to a ConsoleCell buffer.
// All coordinates are in screen-space (column, row) integer cells.
// Clipping is handled per-call to the provided [0, w) x [0, h) region.

#include "../platform/console.h"
#include "../math/vector.h"
#include "../math/rect.h"
#include "../math/color.h"
#include "camera2d.h"
#include <cstring>
#include <cmath>

struct DrawCtx {
    ConsoleCell* buf;    // target cell buffer (row-major)
    int          w, h;  // buffer dimensions (columns × rows)
    Recti        clip;  // active clipping rect (defaults to full buffer)

    DrawCtx(ConsoleCell* b, int w, int h)
        : buf(b), w(w), h(h), clip{0,0,w,h} {}

    // Change clip region; resets on next frame manually
    void setClip(Recti r) { clip = r.intersect({0,0,w,h}); }
    void resetClip()       { clip = {0,0,w,h}; }

    // ── Low-level cell write ──────────────────────────────────────────────────

    FORCE_INLINE bool inClip(int x, int y) const {
        return x>=clip.x && x<clip.right() && y>=clip.y && y<clip.bottom();
    }
    FORCE_INLINE ConsoleCell& cellAt(int x, int y) { return buf[y*w+x]; }
    FORCE_INLINE void put(int x, int y, char ch, RGBColor fg, RGBColor bg={}) {
        if (!inClip(x,y)) return;
        auto& c = cellAt(x, y);
        c.character = ch; c.color = fg; c.bg_color = bg;
        c.is_ui = true;
    }
    FORCE_INLINE void putGlyph(int x, int y, ConsoleGlyph g, RGBColor fg, RGBColor bg={}) {
        if (!inClip(x,y)) return;
        auto& c = cellAt(x, y);
        c.character = g; c.color = fg; c.bg_color = bg;
        c.is_ui = true;
    }

    // ── Filled rectangles ─────────────────────────────────────────────────────

    void fillRect(Recti r, char ch, RGBColor fg, RGBColor bg={}) {
        Recti cr = r.intersect(clip);
        for (int y=cr.y; y<cr.bottom(); ++y)
            for (int x=cr.x; x<cr.right(); ++x)
                put(x, y, ch, fg, bg);
    }
    void fillRect(Recti r, RGBColor bg) { fillRect(r, ' ', bg, bg); }

    // ── Hollow rectangle (border) ─────────────────────────────────────────────

    void drawRect(Recti r, char ch, RGBColor fg, RGBColor bg={}) {
        if (r.w<=0||r.h<=0) return;
        for (int x=r.x; x<r.right(); ++x) {
            put(x, r.y,        ch, fg, bg);
            put(x, r.bottom()-1, ch, fg, bg);
        }
        for (int y=r.y+1; y<r.bottom()-1; ++y) {
            put(r.x,       y, ch, fg, bg);
            put(r.right()-1, y, ch, fg, bg);
        }
    }

    // ── Lines (Bresenham) ─────────────────────────────────────────────────────

    void drawLine(int x0, int y0, int x1, int y1, char ch, RGBColor fg, RGBColor bg={}) {
        int dx=abs(x1-x0), dy=abs(y1-y0);
        int sx=(x0<x1)?1:-1, sy=(y0<y1)?1:-1;
        int err=dx-dy;
        while (true) {
            put(x0, y0, ch, fg, bg);
            if (x0==x1&&y0==y1) break;
            int e2=2*err;
            if (e2>-dy){ err-=dy; x0+=sx; }
            if (e2< dx){ err+=dx; y0+=sy; }
        }
    }

    // ── Circle (Midpoint algorithm) ───────────────────────────────────────────

    void drawCircle(int cx, int cy, int rx, int ry, char ch, RGBColor fg) {
        // Ellipse generalisation (rx = col radius, ry = row radius)
        int x=0, y=ry;
        long rx2=rx*rx, ry2=ry*ry;
        long p=(long)ry2-(long)rx2*ry+(long)(rx2/4);
        auto plot4=[&](int px,int py){ put(cx+px,cy+py,ch,fg); put(cx-px,cy+py,ch,fg);
                                       put(cx+px,cy-py,ch,fg); put(cx-px,cy-py,ch,fg); };
        while (2*ry2*x <= 2*rx2*y) { plot4(x,y); ++x;
            if(p<0) p+=2*ry2*x+ry2; else { --y; p+=2*ry2*x-2*rx2*y+ry2; } }
        x=rx; y=0; p=(long)rx2-(long)ry2*rx+(long)(ry2/4);
        while (2*rx2*y <= 2*ry2*x) { plot4(x,y); ++y;
            if(p<0) p+=2*rx2*y+rx2; else { --x; p+=2*rx2*y-2*ry2*x+rx2; } }
    }
    void drawCircle(int cx, int cy, int r, char ch, RGBColor fg) { drawCircle(cx,cy,r,r/2,ch,fg); }

    // ── Text ──────────────────────────────────────────────────────────────────

    void drawText(int x, int y, const char* text, RGBColor fg, RGBColor bg={}) {
        for (int i=0; text[i]; ++i) put(x+i, y, text[i], fg, bg);
    }
    // Right-aligned text
    void drawTextRight(int rx, int y, const char* text, RGBColor fg, RGBColor bg={}) {
        int len=0; while(text[len]) ++len;
        drawText(rx-len, y, text, fg, bg);
    }
    // Centered text within x1..x2
    void drawTextCenter(int x1, int x2, int y, const char* text, RGBColor fg, RGBColor bg={}) {
        int len=0; while(text[len]) ++len;
        drawText(x1 + (x2-x1-len)/2, y, text, fg, bg);
    }

    // ── Sprite (char array, row-major) ────────────────────────────────────────

    void drawSprite(int x, int y, const char** rows, int sw, int sh, RGBColor fg, char transparent=0) {
        for (int r=0; r<sh; ++r)
            for (int c=0; c<sw; ++c)
                if (rows[r][c] != transparent) put(x+c, y+r, rows[r][c], fg);
    }
};

#endif
