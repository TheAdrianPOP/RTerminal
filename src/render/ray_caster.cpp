#include "ray_caster.h"
#include "triangle.h"
#include "aabb.h"
#include "math_utils.h"

ConsoleCell RayCaster::castRay(const BVH&        bvh,
                               const Ray&        ray,
                               IShadingProvider& provider,
                               float             render_dist,
                               int               screen_x,
                               int               screen_y)
{
    float t_hit = render_dist;

    if (!bvh.nodes.empty()) {
        const AABB& root_box = bvh.nodes[0].bounds;

        if (ray.origin.y > root_box.max.y && ray.direction.y > 0.0f)
            return provider.shadeMiss(ray);

        if (ray.origin.y < root_box.min.y && ray.direction.y < 0.0f)
            return { ' ', {0, 0, 0} };
    }

    const Triangle* hit_tri = nullptr;
    Vec3  n;
    float u, v;

    if (bvh.intersect(ray, t_hit, hit_tri, n, u, v)) {
        if (!hit_tri) return ConsoleCell();

        HitRecord hit;
        hit.point          = ray.origin + ray.direction * t_hit;
        hit.shading_normal = n;
        hit.face_normal    = hit_tri->getFaceNormal();
        hit.triangle       = hit_tri;
        hit.u              = u;
        hit.v              = v;

        return provider.shadeHit(ray, hit, screen_x, screen_y);
    }

    return provider.shadeMiss(ray);
}