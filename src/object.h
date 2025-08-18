#ifndef OBJECT_H
#define OBJECT_H

#include "vector.h"
#include "ray.h"
#include "aabb.h"
#include "material.h"

// Abstract Base Class for Objects
class Object {
public:
    Material material;
    Object(const Material& material) : material(material) {}
    virtual ~Object() = default;
    virtual bool intersect(const Ray& ray, double& t, Vec3& normal, double& u, double& v) const = 0;
    virtual bool getTriangleVertices(Vec3& v0, Vec3& v1, Vec3& v2) const { return false; } // Default implementation
    virtual AABB getAABB() const = 0;
};

#endif // OBJECT_H 