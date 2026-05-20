#ifndef COLLISION_UTILS_H
#define COLLISION_UTILS_H

#include "vector.h"
#include "math_utils.h"

inline Vec3 closestPointOnLineSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    Vec3 ab = b - a;
    float ab_mag_sq = ab.magnitudeSq();
    if (is_zero_f(ab_mag_sq)) return a; 
    
    float t = (p - a).dot(ab) / ab_mag_sq;
    t = fast_clamp(t, 0.0f, 1.0f);
    
    return a + ab * t;
}

inline Vec3 closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    Vec3 ap = p - a;

    float d1 = ab.dot(ap);
    float d2 = ac.dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a; 

    Vec3 bp = p - b;
    float d3 = ab.dot(bp);
    float d4 = ac.dot(bp);
    if (d3 >= 0.0f && d4 <= d3) return b; 

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + ab * v; 
    }

    Vec3 cp = p - c;
    float d5 = ab.dot(cp);
    float d6 = ac.dot(cp);
    if (d6 >= 0.0f && d5 <= d6) return c; 

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + ac * w; 
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w; 
    }

    float denom_inv = 1.0f / (va + vb + vc);
    float v = vb * denom_inv;
    float w = vc * denom_inv;
    return a + ab * v + ac * w; 
}

#endif