#pragma once

#include "bvh.h"
#include "matrix.h"
#include "ray.h"
#include "aabb.h"
#include "math_utils.h"
#include "vector.h"
#include "triangle.h"

static inline AABB tlasTransformAABB(const AABB& box, const Mat4& m) {
    AABB result;
    const Vec3 mn = box.min, mx = box.max;
    for (int i = 0; i < 8; ++i) {
        result.expand(m.transformPoint(Vec3(
            (i & 1) ? mx.x : mn.x,
            (i & 2) ? mx.y : mn.y,
            (i & 4) ? mx.z : mn.z
        )));
    }
    return result;
}

struct BLASInstance {
    const BVH* blas         = nullptr;
    Mat4       to_world;
    Mat4       to_object;
    AABB       world_bounds;
    int        instance_id  = -1;
};

struct TLASHit {
    const Triangle* triangle    = nullptr;
    Vec3            normal;
    float           t           = MAX_FLOAT;
    float           u           = 0.f;
    float           v           = 0.f;
    int             instance_id = -1;

    bool hit() const { return triangle != nullptr; }
};

class TLAS {
public:
    void clear() {
        instances_.clear();
        nodes_.clear();
        indices_.clear();
    }

    int addInstance(const BVH* blas,
                    const Mat4& to_world,
                    const Mat4& to_object,
                    int instance_id = -1)
    {
        if (!blas || blas->nodes.empty()) return -1;
        BLASInstance inst;
        inst.blas         = blas;
        inst.to_world     = to_world;
        inst.to_object    = to_object;
        inst.instance_id  = instance_id;
        inst.world_bounds = tlasTransformAABB(blas->nodes[0].bounds, to_world);
        instances_.push_back(inst);
        return (int)instances_.size() - 1;
    }

    void setTransform(int slot, const Mat4& to_world, const Mat4& to_object) {
        if (slot < 0 || slot >= (int)instances_.size()) return;
        BLASInstance& inst = instances_[slot];
        inst.to_world  = to_world;
        inst.to_object = to_object;
        if (inst.blas && !inst.blas->nodes.empty())
            inst.world_bounds = tlasTransformAABB(inst.blas->nodes[0].bounds, to_world);
    }

    void build() {
        nodes_.clear();
        indices_.clear();
        if (instances_.empty()) return;

        indices_.resize(instances_.size());
        for (size_t i = 0; i < instances_.size(); ++i) indices_[i] = (int)i;
        nodes_.reserve(instances_.size() * 2);
        buildNode(0, (int)instances_.size());
    }

    void refit() {
        for (int i = (int)nodes_.size() - 1; i >= 0; --i) {
            TLASNode& n = nodes_[i];
            if (n.count > 0) {
                AABB box;
                const int end = n.leftOrFirst + n.count;
                for (int k = n.leftOrFirst; k < end; ++k)
                    box.expand(instances_[indices_[k]].world_bounds);
                n.bounds = box;
            } else {
                const AABB& lb = nodes_[i + 1].bounds;
                const AABB& rb = nodes_[n.leftOrFirst].bounds;
                n.bounds.min = lb.min.min(rb.min);
                n.bounds.max = lb.max.max(rb.max);
            }
        }
    }

    TLASHit intersect(const Ray& world_ray, float max_t) const {
        TLASHit result;
        if (nodes_.empty()) return result;

        int   stack[kTlasStackDepth];
        int   ptr   = 0;
        float t_hit = max_t;
        stack[ptr++] = 0;

        while (ptr > 0) {
            const int       nodeIdx = stack[--ptr];
            const TLASNode& node    = nodes_[nodeIdx];

            if (node.bounds.intersect(world_ray, EPSILON_ABS, t_hit) == MAX_FLOAT)
                continue;

            if (node.count > 0) {
                const int end = node.leftOrFirst + node.count;
                for (int k = node.leftOrFirst; k < end; ++k) {
                    const BLASInstance& inst = instances_[indices_[k]];
                    Ray obj_ray(
                        inst.to_object.transformPoint(world_ray.origin),
                        inst.to_object.transformDir(world_ray.direction)
                    );

                    float           t_local = t_hit;
                    const Triangle* tri     = nullptr;
                    Vec3            n_local;
                    float           u, v;

                    if (inst.blas->intersect(obj_ray, t_local, tri, n_local, u, v)
                        && t_local < t_hit)
                    {
                        t_hit              = t_local;
                        result.triangle    = tri;
                        result.normal      = inst.to_world.transformDir(n_local).normalize();
                        result.t           = t_local;
                        result.u           = u;
                        result.v           = v;
                        result.instance_id = inst.instance_id;
                    }
                }
            } else {
                const int   L   = nodeIdx + 1;
                const int   R   = node.leftOrFirst;
                const float t_L = nodes_[L].bounds.intersect(world_ray, EPSILON_ABS, t_hit);
                const float t_R = nodes_[R].bounds.intersect(world_ray, EPSILON_ABS, t_hit);
                if (t_L < t_R) {
                    if (t_R != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = R;
                    if (t_L != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = L;
                } else {
                    if (t_L != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = L;
                    if (t_R != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = R;
                }
            }
        }
        return result;
    }

    bool intersectsAny(const Ray& world_ray, float max_t) const {
        if (nodes_.empty()) return false;

        int stack[kTlasStackDepth];
        int ptr = 0;
        stack[ptr++] = 0;

        while (ptr > 0) {
            const int       nodeIdx = stack[--ptr];
            const TLASNode& node    = nodes_[nodeIdx];

            if (node.bounds.intersect(world_ray, EPSILON_ABS, max_t) == MAX_FLOAT)
                continue;

            if (node.count > 0) {
                const int end = node.leftOrFirst + node.count;
                for (int k = node.leftOrFirst; k < end; ++k) {
                    const BLASInstance& inst = instances_[indices_[k]];
                    Ray obj_ray(
                        inst.to_object.transformPoint(world_ray.origin),
                        inst.to_object.transformDir(world_ray.direction)
                    );
                    if (inst.blas->intersectsAny(obj_ray, max_t)) return true;
                }
            } else {
                const int   L   = nodeIdx + 1;
                const int   R   = node.leftOrFirst;
                const float t_L = nodes_[L].bounds.intersect(world_ray, EPSILON_ABS, max_t);
                const float t_R = nodes_[R].bounds.intersect(world_ray, EPSILON_ABS, max_t);
                if (t_L < t_R) {
                    if (t_R != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = R;
                    if (t_L != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = L;
                } else {
                    if (t_L != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = L;
                    if (t_R != MAX_FLOAT && ptr < kTlasStackSafeLimit) stack[ptr++] = R;
                }
            }
        }
        return false;
    }

    size_t instanceCount() const { return instances_.size(); }
    bool   empty()         const { return instances_.empty(); }

private:
    struct TLASNode {
        AABB bounds;
        int  leftOrFirst = 0;
        int  count       = 0;
    };

    PodVector<BLASInstance> instances_;
    PodVector<TLASNode>     nodes_;
    PodVector<int>          indices_;

    int buildNode(int start, int end) {
        const int count   = end - start;
        const int nodeIdx = (int)nodes_.size();
        nodes_.push_back({});

        AABB bounds;
        for (int i = start; i < end; ++i)
            bounds.expand(instances_[indices_[i]].world_bounds);
        nodes_[nodeIdx].bounds = bounds;

        if (count <= kTlasMaxLeafInstances) {
            nodes_[nodeIdx].leftOrFirst = start;
            nodes_[nodeIdx].count       = count;
            return nodeIdx;
        }

        const Vec3 extent = bounds.max - bounds.min;
        int axis = 0;
        if (extent.y >= extent.x && extent.y >= extent.z) axis = 1;
        else if (extent.z >= extent.x)                    axis = 2;

        const float axisMin = (axis == 0 ? bounds.min.x : axis == 1 ? bounds.min.y : bounds.min.z);
        const float axisLen = (axis == 0 ? extent.x     : axis == 1 ? extent.y     : extent.z);

        if (is_zero_f(axisLen)) {
            nodes_[nodeIdx].leftOrFirst = start;
            nodes_[nodeIdx].count       = count;
            return nodeIdx;
        }

        constexpr int BINS = 8;
        struct Bin { AABB bounds; int count = 0; };
        Bin bins[BINS];
        const float binScale = (float)BINS / axisLen;

        for (int i = start; i < end; ++i) {
            const AABB& wb  = instances_[indices_[i]].world_bounds;
            const Vec3  c   = (wb.min + wb.max) * 0.5f;
            const float val = (axis == 0 ? c.x : axis == 1 ? c.y : c.z);
            const int   b   = fast_clamp((int)((val - axisMin) * binScale), 0, BINS - 1);
            bins[b].count++;
            bins[b].bounds.expand(wb);
        }

        float leftArea[BINS - 1]; int leftCnt[BINS - 1];
        AABB  lBox; int lC = 0;
        for (int i = 0; i < BINS - 1; ++i) {
            lC += bins[i].count; lBox.expand(bins[i].bounds);
            leftCnt[i] = lC; leftArea[i] = lBox.surfaceArea();
        }

        float bestCost  = MAX_FLOAT;
        int   bestSplit = BINS / 2;
        AABB  rBox; int rC = 0;
        for (int i = BINS - 1; i > 0; --i) {
            rC += bins[i].count; rBox.expand(bins[i].bounds);
            const int si = i - 1;
            if (leftCnt[si] > 0 && rC > 0) {
                const float cost = leftArea[si] * (float)leftCnt[si]
                                 + rBox.surfaceArea() * (float)rC;
                if (cost < bestCost) { bestCost = cost; bestSplit = i; }
            }
        }

        const float splitPos = axisMin + (float)bestSplit * (axisLen / BINS);

        int lo = start, hi = end - 1;
        while (lo <= hi) {
            const AABB& wb  = instances_[indices_[lo]].world_bounds;
            const Vec3  c   = (wb.min + wb.max) * 0.5f;
            const float val = (axis == 0 ? c.x : axis == 1 ? c.y : c.z);
            if (val < splitPos) { ++lo; }
            else { int tmp = indices_[lo]; indices_[lo] = indices_[hi]; indices_[hi] = tmp; --hi; }
        }
        const int mid = lo;

        if (mid == start || mid == end) {
            nodes_[nodeIdx].leftOrFirst = start;
            nodes_[nodeIdx].count       = count;
            return nodeIdx;
        }

        nodes_[nodeIdx].count = 0;
        buildNode(start, mid);
        const int rightChild = buildNode(mid, end);
        nodes_[nodeIdx].leftOrFirst = rightChild;
        return nodeIdx;
    }
};