#ifndef SHADING_PROVIDER_H
#define SHADING_PROVIDER_H

#include "console.h"
#include "ray.h"
#include "triangle.h"
#include "vector.h"

struct HitRecord {
    Vec3            point;
    Vec3            shading_normal;
    Vec3            face_normal;
    const Triangle* triangle = nullptr;
    float           u = 0.f, v = 0.f;
};

class IShadingProvider {
public:
    virtual ~IShadingProvider() = default;
    
    virtual void prepareFrame() {}

    virtual ConsoleCell shadeHit(const Ray&       ray,
                                  const HitRecord& hit,
                                  int screen_x, int screen_y) = 0;

    virtual ConsoleCell shadeMiss(const Ray& ray) = 0;
};

#endif