#pragma once
#ifndef CURVE_H
#define CURVE_H

#include "vector.h"
#include "easing.h"
#include "arena.h"

// ─── Bezier ──────────────────────────────────────────────────────────────────

FORCE_INLINE Vec2 bezierQuad(Vec2 p0, Vec2 p1, Vec2 p2, float t) {
    float it = 1.f - t;
    return p0*(it*it) + p1*(2*it*t) + p2*(t*t);
}
FORCE_INLINE Vec2 bezierQuadTangent(Vec2 p0, Vec2 p1, Vec2 p2, float t) {
    return (p1-p0)*2*(1-t) + (p2-p1)*2*t;
}

FORCE_INLINE Vec2 bezierCubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    float it = 1.f - t, it2=it*it, t2=t*t;
    return p0*(it2*it) + p1*(3*it2*t) + p2*(3*it*t2) + p3*(t2*t);
}
FORCE_INLINE Vec2 bezierCubicTangent(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    float it = 1.f-t;
    return (p1-p0)*(3*it*it) + (p2-p1)*(6*it*t) + (p3-p2)*(3*t*t);
}

// ─── Catmull-Rom Spline ───────────────────────────────────────────────────────

FORCE_INLINE Vec2 catmullRom(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
    float t2 = t*t, t3 = t2*t;
    return (p0*(-t3+2*t2-t) + p1*(3*t3-5*t2+2) + p2*(-3*t3+4*t2+t) + p3*(t3-t2)) * 0.5f;
}

// ─── Spline ──────────────────────────────────────────────────────────────────

struct Spline {
    static constexpr int kMaxPoints = 64;
    Vec2  points[kMaxPoints];
    int   count = 0;

    void add(Vec2 p) { if (count < kMaxPoints) points[count++] = p; }
    void clear()     { count = 0; }

    Vec2 evaluate(float t) const {
        if (count < 2) return count == 1 ? points[0] : Vec2{};
        t = fast_clamp(t, 0.f, (float)(count - 1));
        int   seg = (int)t; if (seg >= count-1) seg = count-2;
        float lt  = t - (float)seg;
        Vec2 p0 = points[seg > 0         ? seg-1 : 0      ];
        Vec2 p1 = points[seg                               ];
        Vec2 p2 = points[seg+1 < count   ? seg+1 : count-1];
        Vec2 p3 = points[seg+2 < count   ? seg+2 : count-1];
        return catmullRom(p0, p1, p2, p3, lt);
    }

    float arcLength(int samplesPerSeg = 8) const {
        if (count < 2) return 0.f;
        float len = 0.f;
        int segs  = count - 1;
        for (int s = 0; s < segs; ++s) {
            Vec2 prev = evaluate((float)s);
            for (int k = 1; k <= samplesPerSeg; ++k) {
                Vec2 cur = evaluate((float)s + (float)k / samplesPerSeg);
                len += (cur - prev).length();
                prev = cur;
            }
        }
        return len;
    }

    Vec2 evaluateNorm(float u, int samplesPerSeg = 8) const {
        float total = arcLength(samplesPerSeg);
        if (total < 1e-5f) return evaluate(0.f);
        float target = fast_clamp(u, 0.f, 1.f) * total;
        float acc = 0.f;
        int segs  = count - 1;
        for (int s = 0; s < segs; ++s) {
            Vec2 prev = evaluate((float)s);
            for (int k = 1; k <= samplesPerSeg; ++k) {
                float tp = (float)s + (float)k / samplesPerSeg;
                Vec2  cur = evaluate(tp);
                float step = (cur - prev).length();
                if (acc + step >= target) {
                    float frac = (target - acc) / fast_max(step, 1e-5f);
                    return prev.lerp(cur, frac);
                }
                acc += step; prev = cur;
            }
        }
        return evaluate((float)(count - 1));
    }
};

// ─── Keyframe float curve ─────────────────────────────────────────────────────

struct FloatCurve {
    static constexpr int kMaxKeys = 32;
    struct Key { float time, value; };
    Key  keys[kMaxKeys];
    int  count = 0;

    void addKey(float t, float v)  { if (count<kMaxKeys) keys[count++]={t,v}; }
    void clear()                   { count=0; }

    float evaluate(float t) const {
        if (count == 0) return 0.f;
        if (count == 1 || t <= keys[0].time)  return keys[0].value;
        if (t >= keys[count-1].time)           return keys[count-1].value;
        for (int i = 0; i < count-1; ++i) {
            if (t < keys[i+1].time) {
                float nt = (t - keys[i].time) / (keys[i+1].time - keys[i].time);
                return fast_lerp(keys[i].value, keys[i+1].value, nt);
            }
        }
        return keys[count-1].value;
    }
};

#endif // CURVE_H