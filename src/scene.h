#ifndef SCENE_H
#define SCENE_H

#include "triangle.h" // Includes object.h, material.h, etc. indirectly
#include "light.h"
#include "bvh.h"      // Includes aabb.h, object.h, ray.h indirectly
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <chrono>
#include <iostream> // For build timing output

struct Scene {
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<Light> lights;
    std::map<std::string, Material> materials;
    BVH bvh;

    void addQuad(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3,
        const Vec2& uv0, const Vec2& uv1, const Vec2& uv2, const Vec2& uv3,
        const Material& material) {
        objects.push_back(std::make_unique<Triangle>(v0, v1, v2, uv0, uv1, uv2, material));
        objects.push_back(std::make_unique<Triangle>(v0, v2, v3, uv0, uv2, uv3, material));
    }

    void buildAccelerationStructure() {
        auto start_time = std::chrono::high_resolution_clock::now();
        bvh.build(objects);
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration_ms = end_time - start_time;
    }
};

#endif // SCENE_H 