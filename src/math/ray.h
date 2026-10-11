#ifndef RAY_H
#define RAY_H

#include "vector.h"
#include "math_utils.h"

struct Ray {
    Vec3 origin;
    Vec3 direction;
    Vec3 inv_direction;

    FORCE_INLINE Ray(const Vec3& origin, const Vec3& dir) 
        : origin(origin), direction(dir.normalize()) {
        
        inv_direction = Vec3(_mm_rcp_ps(direction.load()));
    }
};

#endif // RAY_H