#ifndef COLLISION_UTILS_H
#define COLLISION_UTILS_H

#include "vector.h"
#include "constants.h" // For EPSILON
#include <algorithm> // For std::clamp
#include <cmath>     // For std::sqrt in Vec3::magnitude

// Closest point on line segment AB to point P
inline Vec3 closestPointOnLineSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    Vec3 ab = b - a;
    double ab_mag_sq = ab.magnitudeSq();
    if (ab_mag_sq < EPSILON * EPSILON) return a; // Handle zero-length segment
    double t = (p - a).dot(ab) / ab_mag_sq;
    t = std::clamp(t, 0.0, 1.0);
    return a + ab * t;
}

// Closest point on triangle ABC to point P
inline Vec3 closestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    Vec3 ap = p - a;

    double d1 = ab.dot(ap);
    double d2 = ac.dot(ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a; // Barycentric coordinates (1,0,0)

    Vec3 bp = p - b;
    double d3 = ab.dot(bp);
    double d4 = ac.dot(bp);
    if (d3 >= 0.0 && d4 <= d3) return b; // Barycentric coordinates (0,1,0)

    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        double v = d1 / (d1 - d3);
        return a + ab * v; // Barycentric coordinates (1-v,v,0)
    }

    Vec3 cp = p - c;
    double d5 = ab.dot(cp);
    double d6 = ac.dot(cp);
    if (d6 >= 0.0 && d5 <= d6) return c; // Barycentric coordinates (0,0,1)

    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        double w = d2 / (d2 - d6);
        return a + ac * w; // Barycentric coordinates (1-w,0,w)
    }

    double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w; // Barycentric coordinates (0,1-w,w)
    }

    // Inside feature region - compute barycentric coordinates
    double denom_inv = 1.0 / (va + vb + vc);
    double v = vb * denom_inv;
    double w = vc * denom_inv;
    return a + ab * v + ac * w; // u = 1.0 - v - w
}

#endif // COLLISION_UTILS_H 