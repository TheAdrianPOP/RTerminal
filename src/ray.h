#ifndef RAY_H
#define RAY_H

#include "vector.h"

struct Ray {
    Vec3 origin;
    Vec3 direction; // Assume normalized
    Ray(const Vec3& origin, const Vec3& direction) : origin(origin), direction(direction.normalize()) {} // Ensure direction is normalized on construction
};

#endif // RAY_H 