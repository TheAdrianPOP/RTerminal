#ifndef AABB_H
#define AABB_H

#include "vector.h"
#include "ray.h"
#include "math_utils.h"

struct alignas(16) AABB {
    Vec3 min;
    Vec3 max;

    FORCE_INLINE AABB() 
        : min(MAX_FLOAT, MAX_FLOAT, MAX_FLOAT), 
          max(MIN_FLOAT, MIN_FLOAT, MIN_FLOAT) {}

    FORCE_INLINE AABB(const Vec3& p_min, const Vec3& p_max) 
        : min(p_min), max(p_max) {}

    FORCE_INLINE void expand(const Vec3& p) {
        min = min.min(p);
        max = max.max(p);
    }

    FORCE_INLINE void expand(const AABB& other) {
        min = min.min(other.min);
        max = max.max(other.max);
    }

    FORCE_INLINE float intersect(const Ray& ray, float t_min, float t_max) const {
        Vec3 t0 = (min - ray.origin) * ray.inv_direction;
        Vec3 t1 = (max - ray.origin) * ray.inv_direction;

        Vec3 t_small = t0.min(t1);
        float t_enter = fast_max(t_small.hmax3(), t_min);

        if (t_enter >= t_max) return MAX_FLOAT;

        Vec3 t_big   = t0.max(t1);
        float t_exit  = fast_min(t_big.hmin3(), t_max);

        return (t_exit >= t_enter) ? t_enter : MAX_FLOAT;
    }

    FORCE_INLINE Vec3 center() const { return (min + max) * 0.5f; }

    FORCE_INLINE float surfaceArea() const {
        Vec3 d = max - min;
        return 2.0f * (d.x * d.y + d.x * d.z + d.y * d.z);
    }
    
    bool overlaps(const AABB& other) const {
        return (max.x >= other.min.x && min.x <= other.max.x &&
                max.y >= other.min.y && min.y <= other.max.y &&
                max.z >= other.min.z && min.z <= other.max.z);
    }
};

#endif