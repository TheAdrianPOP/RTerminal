#pragma once

#include "engine_config.h"
#include "vector.h"
#include "math_utils.h"
#include "aabb.h"
#include "collision_utils.h"
#include "triangle.h"

enum class ShapeType { Capsule, MultiSphere };
struct CapsuleData {
    float radius      = kCapsuleDefaultRadius;
    float half_height = kCapsuleDefaultHalfHeight;
    Vec3  offset;

    FORCE_INLINE Vec3 segmentA(const Vec3& pos) const {
        return pos + offset - Vec3(0.f, half_height, 0.f);
    }
    FORCE_INLINE Vec3 segmentB(const Vec3& pos) const {
        return pos + offset + Vec3(0.f, half_height, 0.f);
    }
};

struct SpherePrimitive {
    Vec3  offset;
    float radius;
};

struct MultiSphereData {
    static constexpr int MAX_SPHERES = kMaxSpheresPerShape;
    SpherePrimitive spheres[MAX_SPHERES];
    int             count = 0;

    void add(Vec3 local_offset, float radius) {
        if (count < MAX_SPHERES)
            spheres[count++] = { local_offset, radius };
    }

    float maxRadius() const {
        float r = 0.f;
        for (int i = 0; i < count; ++i) r = fast_max(r, spheres[i].radius);
        return r;
    }

    float maxY() const {
        float y = 0.f;
        for (int i = 0; i < count; ++i)
            y = fast_max(y, spheres[i].offset.y + spheres[i].radius);
        return y;
    }
};

struct CollisionShape {
    ShapeType type = ShapeType::Capsule;

    union {
        CapsuleData     capsule;
        MultiSphereData multi;
    };

    CollisionShape() : type(ShapeType::Capsule), capsule() {}

    static CollisionShape makeCapsule(float radius, float total_height,
                                       Vec3 offset = Vec3(0.f, 0.f, 0.f)) {
        CollisionShape s;
        s.type                = ShapeType::Capsule;
        s.capsule.radius      = radius;
        s.capsule.half_height = fast_max(0.f, total_height * 0.5f - radius);
        s.capsule.offset      = offset;
        return s;
    }

    static CollisionShape makeMultiSphere() {
        CollisionShape s;
        s.type        = ShapeType::MultiSphere;
        s.multi.count = 0;
        return s;
    }

    AABB getQueryAABB(const Vec3& pos) const {
        AABB box;
        if (type == ShapeType::Capsule) {
            float r = capsule.radius;
            Vec3  rv(r, r, r);
            box.expand(capsule.segmentA(pos) - rv);
            box.expand(capsule.segmentB(pos) + rv);
        } else {
            for (int i = 0; i < multi.count; ++i) {
                float r = multi.spheres[i].radius;
                Vec3  c = pos + multi.spheres[i].offset;
                Vec3  rv(r, r, r);
                box.expand(c - rv);
                box.expand(c + rv);
            }
        }
        return box;
    }

    Vec3 getBase(const Vec3& pos) const {
        if (type == ShapeType::Capsule)
            return capsule.segmentA(pos);
        if (multi.count > 0) return pos + multi.spheres[0].offset;
        return pos;
    }
};

struct CollisionResult {
    Vec3 position;
    Vec3 velocity;
    bool ground_hit;
};

inline CollisionResult resolveShape(
    const CollisionShape&          shape,
    Vec3                           position,
    Vec3                           velocity,
    const PodVector<const Triangle*>& colliders,
    int   steps,
    float push_factor,
    float slope_threshold)
{
    bool ground_hit = false;

    for (int step = 0; step < steps; ++step) {
        bool any_hit = false;

        if (shape.type == ShapeType::Capsule) {
            const CapsuleData& cap = shape.capsule;
            Vec3 segA = cap.segmentA(position);
            Vec3 segB = cap.segmentB(position);
            float r   = cap.radius;
            float rSq = r * r;

            for (const Triangle* tri : colliders) {
                Vec3 triPt = closestPointOnTriangle(segA,
                                 tri->v0, tri->getV1(), tri->getV2());
                Vec3 segPt = closestPointOnLineSegment(triPt, segA, segB);
                Vec3 diff  = segPt - triPt;
                float dSq  = diff.magnitudeSq();

                if (dSq < rSq - EPSILON_ABS) {
                    any_hit   = true;
                    float d   = (dSq > EPSILON_ABS) ? fast_sqrt(dSq) : 0.f;
                    Vec3  n   = (d   > EPSILON_ABS) ? diff.normalize() : Vec3(0.f,1.f,0.f);
                    Vec3  push = n * (r - d + push_factor);

                    position += push;
                    segA     += push;
                    segB     += push;

                    float vdot = velocity.dot(n);
                    if (vdot < 0.f) velocity = velocity - n * vdot;
                    if (n.y > slope_threshold) ground_hit = true;
                }
            }

        } else {
            for (int si = 0; si < shape.multi.count; ++si) {
                const SpherePrimitive& sp = shape.multi.spheres[si];
                Vec3  center = position + sp.offset;
                float r      = sp.radius;
                float rSq    = r * r;

                for (const Triangle* tri : colliders) {
                    Vec3  closest = closestPointOnTriangle(center,
                                        tri->v0, tri->getV1(), tri->getV2());
                    Vec3  diff    = center - closest;
                    float dSq     = diff.magnitudeSq();

                    if (dSq < rSq - EPSILON_ABS) {
                        any_hit    = true;
                        float d    = (dSq > EPSILON_ABS) ? fast_sqrt(dSq) : 0.f;
                        Vec3  n    = (d   > EPSILON_ABS) ? diff.normalize() : Vec3(0.f,1.f,0.f);
                        Vec3  push = n * (r - d + push_factor);

                        position += push;
                        center   += push;

                        float vdot = velocity.dot(n);
                        if (vdot < 0.f) velocity = velocity - n * vdot;
                        if (n.y > slope_threshold) ground_hit = true;
                    }
                }
            }
        }

        if (!any_hit) break;
    }

    return { position, velocity, ground_hit };
}