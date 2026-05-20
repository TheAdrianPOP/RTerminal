#pragma once

#include "ray.h"
#include "bvh.h"
#include "shading_provider.h"
#include "console.h"

class RayCaster {
public:
    static ConsoleCell castRay(const BVH&        bvh,
                                const Ray&        ray,
                                IShadingProvider& provider,
                                float             render_dist,
                                int               screen_x = 0,
                                int               screen_y = 0);
};