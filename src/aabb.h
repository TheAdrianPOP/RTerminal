#ifndef AABB_H
#define AABB_H

#include "vector.h"
#include "ray.h"
#include "constants.h"
#include <limits>
#include <algorithm>
#include <cmath> // For std::swap needed in intersect

struct AABB {
    Vec3 min = Vec3(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());
    Vec3 max = Vec3(-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity());

    AABB() = default;
    AABB(const Vec3& p_min, const Vec3& p_max) : min(p_min), max(p_max) {}

    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    void expand(const AABB& other) {
        expand(other.min);
        expand(other.max);
    }

    // Modified intersect: returns intersection distance 't' or infinity if no hit within [t_min, t_max]
    double intersect(const Ray& ray, double t_min, double t_max) const {
        // Using a robust slab test implementation
        // Handle division by zero: 1.0/0.0 gives +/- INF, which works with min/max.
        Vec3 inv_dir(1.0 / ray.direction.x, 1.0 / ray.direction.y, 1.0 / ray.direction.z);

        double tx_min = (min.x - ray.origin.x) * inv_dir.x;
        double tx_max = (max.x - ray.origin.x) * inv_dir.x;
        // Swap if ray direction component is negative
        if (inv_dir.x < 0.0) std::swap(tx_min, tx_max);

        double ty_min = (min.y - ray.origin.y) * inv_dir.y;
        double ty_max = (max.y - ray.origin.y) * inv_dir.y;
        if (inv_dir.y < 0.0) std::swap(ty_min, ty_max);

        double tz_min = (min.z - ray.origin.z) * inv_dir.z;
        double tz_max = (max.z - ray.origin.z) * inv_dir.z;
        if (inv_dir.z < 0.0) std::swap(tz_min, tz_max);

        // Find the intersection interval of the slabs
        double t_enter_slab = std::max({tx_min, ty_min, tz_min});
        double t_exit_slab = std::min({tx_max, ty_max, tz_max});

        // Check if the slab intersection interval is valid and overlaps the ray's valid interval [t_min, t_max]
        if (t_enter_slab > t_exit_slab          // Ray misses the box entirely or enters after exiting
            || t_exit_slab < t_min              // Box is entirely "before" the ray segment starts
            || t_enter_slab > t_max) {          // Box is entirely "after" the ray segment ends
            return std::numeric_limits<double>::infinity(); // No valid intersection within the range
        }

        // Return the actual entry point 't', clamped by the ray's minimum valid t (t_min)
        // If t_enter_slab is less than t_min, it means the ray starts inside the box's slab,
        // so the effective intersection point is t_min.
        return std::max(t_min, t_enter_slab);
    }

    Vec3 center() const {
        return (min + max) * 0.5;
    }

    double surfaceArea() const {
        Vec3 d = max - min;
        // Ensure dimensions are valid before calculating area
        if (d.x < 0 || d.y < 0 || d.z < 0) return 0.0;
        return 2.0 * (d.x * d.y + d.x * d.z + d.y * d.z);
    }

    bool overlaps(const AABB& other) const {
        if (max.x < other.min.x || min.x > other.max.x) return false;
        if (max.y < other.min.y || min.y > other.max.y) return false;
        if (max.z < other.min.z || min.z > other.max.z) return false;
        return true; // Overlapping on all axes
    }
};

#endif // AABB_H 