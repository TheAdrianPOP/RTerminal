#ifndef LIGHT_H
#define LIGHT_H

#include "vector.h"

struct Light {
    Vec3 position;
    Vec3 direction;
    Vec3 color;
    double intensity;
    Light(const Vec3& pos, const Vec3& col, double intens, const Vec3& d = Vec3(0, -1, 0)) : position(pos), direction(d.normalize()), color(col), intensity(intens) {}
};

#endif // LIGHT_H 