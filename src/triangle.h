#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "object.h"
#include "vector.h"
#include "constants.h"
#include <cmath> // For std::sqrt in Vec3::normalize if not included via vector.h

class Triangle : public Object {
public:
    Vec3 v0, v1, v2;   // Vertices
    Vec2 uv0, uv1, uv2; // Texture coordinates per vertex
    Vec3 edge1, edge2; // Edges for intersection calculation
    Vec3 faceNormal;   // Precomputed face normal

    Triangle(const Vec3& v0, const Vec3& v1, const Vec3& v2,
        const Vec2& uv0, const Vec2& uv1, const Vec2& uv2,
        const Material& material)
        : Object(material), v0(v0), v1(v1), v2(v2), uv0(uv0), uv1(uv1), uv2(uv2) {
        edge1 = v1 - v0;
        edge2 = v2 - v0;
        faceNormal = edge1.cross(edge2).normalize();
    }

    // Möller–Trumbore intersection algorithm
    bool intersect(const Ray& ray, double& t, Vec3& out_normal, double& u, double& v) const override {
        Vec3 pvec = ray.direction.cross(edge2);
        double det = edge1.dot(pvec);
        if (det < EPSILON) return false; // Back-face culling

        double invDet = 1.0 / det;
        Vec3 tvec = ray.origin - v0;
        double b1 = tvec.dot(pvec) * invDet; // Barycentric coord u
        if (b1 < -EPSILON || b1 > 1.0 + EPSILON) return false;

        Vec3 qvec = tvec.cross(edge1);
        double b2 = ray.direction.dot(qvec) * invDet; // Barycentric coord v
        if (b2 < -EPSILON || b1 + b2 > 1.0 + EPSILON) return false; // Check if inside

        t = edge2.dot(qvec) * invDet; // Distance t

        if (t > EPSILON) {
            out_normal = faceNormal;
            double b0 = 1.0 - b1 - b2;
            u = uv0.u * b0 + uv1.u * b1 + uv2.u * b2;
            v = uv0.v * b0 + uv1.v * b1 + uv2.v * b2;
            return true;
        }
        return false;
    }

    bool getTriangleVertices(Vec3& out_v0, Vec3& out_v1, Vec3& out_v2) const override {
        out_v0 = v0;
        out_v1 = v1;
        out_v2 = v2;
        return true;
    }

    AABB getAABB() const override {
        AABB box;
        box.expand(v0);
        box.expand(v1);
        box.expand(v2);
        const double padding = 1e-4; // Use a smaller padding or make it a constant?
        box.min = box.min - Vec3(padding, padding, padding);
        box.max = box.max + Vec3(padding, padding, padding);
        return box;
    }
};

#endif // TRIANGLE_H 