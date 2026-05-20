#ifndef BVH_H
#define BVH_H

#include "aabb.h"
#include "triangle.h"
#include "ray.h"
#include "engine_config.h"
#include "math_utils.h"
#include "perf_timer.h"
#include "vector.h"
#include "atomic.h"

struct alignas(32) FlatBVHNode {
    AABB bounds;
    int leftOrFirst; 
    int count;       
};

struct alignas(128) QBVHNode {
    SimdFloat4 b_min_x, b_min_y, b_min_z;
    SimdFloat4 b_max_x, b_max_y, b_max_z;
    int children[4];   
    int num_packs[4];
};

struct Bin {
    AABB bounds;
    int count = 0;
};

class BVH {
public:
    PodVector<FlatBVHNode> nodes;
    PodVector<QBVHNode> qnodes;
    PodVector<int> triangle_indices;
    PodVector<TrianglePack> packed_triangles;
    const PodVector<Triangle>* source_triangles = nullptr;

    void build(const PodVector<Triangle>& tris) {
        if (tris.empty()) return;
        source_triangles = &tris;
        triangle_indices.resize(tris.size());
        for(size_t i=0; i<tris.size(); ++i) triangle_indices[i] = (int)i;
        
        cached_centers.resize(tris.size());
        cached_boxes.resize(tris.size());
        for (size_t i = 0; i < tris.size(); ++i) {
            cached_boxes[i] = tris[i].getAABB();
            cached_centers[i] = tris[i].getCentroid();
        }

        PodVector<FlatBVHNode> bin_nodes;
        bin_nodes.reserve(tris.size() * 2);
        buildRecursive(0, (int)tris.size(), bin_nodes);

        packed_triangles.clear();
        packed_triangles.reserve(tris.size() / kTriPackLanes + bin_nodes.size());

        for(auto& node : bin_nodes) {
            if (node.count > 0) {
                int start = node.leftOrFirst;
                int count = node.count;
                int pack_start = (int)packed_triangles.size();
                int num_packs = (count + kTriPackLanes - 1) / kTriPackLanes;

                for(int p=0; p<num_packs; ++p) {
                    TrianglePack pack;
                    for(int k=0; k<kTriPackLanes; ++k) {
                        int idx_in_node = p*kTriPackLanes + k;
                        if(idx_in_node < count) {
                            int tri_idx = triangle_indices[start + idx_in_node];
                            pack.set(k, (*source_triangles)[tri_idx], tri_idx);
                            pack.count++;
                        }
                    }
                    packed_triangles.push_back(pack);
                }
                node.leftOrFirst = pack_start;
                node.count = num_packs;
            }
        }

        qnodes.clear();
        qnodes.reserve(bin_nodes.size()); 
        collapseToQBVH(0, bin_nodes);
        qnodes.shrink_to_fit();

        nodes.clear();
        nodes.push_back({});
        nodes[0].bounds = bin_nodes[0].bounds;

        cached_centers.clear(); cached_boxes.clear();
        cached_centers.shrink_to_fit(); cached_boxes.shrink_to_fit();
    }

    void refit() {
        if (!source_triangles || qnodes.empty()) return;

        for (auto& pack : packed_triangles) {
            for (int k = 0; k < pack.count; ++k) {
                const int ti = pack.indices[k];
                if (ti >= 0) pack.set(k, (*source_triangles)[ti], ti);
            }
        }

        for (int i = (int)qnodes.size() - 1; i >= 0; --i) {
            QBVHNode& q = qnodes[i];
            alignas(16) float mx[4], my[4], mz[4];
            alignas(16) float Mx[4], My[4], Mz[4];
            _mm_store_ps(mx, q.b_min_x.m); _mm_store_ps(my, q.b_min_y.m); _mm_store_ps(mz, q.b_min_z.m);
            _mm_store_ps(Mx, q.b_max_x.m); _mm_store_ps(My, q.b_max_y.m); _mm_store_ps(Mz, q.b_max_z.m);
            
            for (int c = 0; c < 4; ++c) {
                if (q.num_packs[c] > 0) {
                    AABB box;
                    int end = q.children[c] + q.num_packs[c];
                    for(int p = q.children[c]; p < end; ++p) {
                        const TrianglePack& pack = packed_triangles[p];
                        for(int k = 0; k < pack.count; ++k) {
                            if(pack.indices[k] >= 0) box.expand((*source_triangles)[pack.indices[k]].getAABB());
                        }
                    }
                    mx[c] = box.min.x; my[c] = box.min.y; mz[c] = box.min.z;
                    Mx[c] = box.max.x; My[c] = box.max.y; Mz[c] = box.max.z;
                } 
                else if (q.num_packs[c] == 0) {
                    const QBVHNode& child = qnodes[q.children[c]];
                    mx[c] = simd_hmin(child.b_min_x); my[c] = simd_hmin(child.b_min_y); mz[c] = simd_hmin(child.b_min_z);
                    Mx[c] = simd_hmax(child.b_max_x); My[c] = simd_hmax(child.b_max_y); Mz[c] = simd_hmax(child.b_max_z);
                }
            }
            q.b_min_x = SimdFloat4(mx); q.b_min_y = SimdFloat4(my); q.b_min_z = SimdFloat4(mz);
            q.b_max_x = SimdFloat4(Mx); q.b_max_y = SimdFloat4(My); q.b_max_z = SimdFloat4(Mz);
        }

        nodes[0].bounds.min = Vec3(simd_hmin(qnodes[0].b_min_x), simd_hmin(qnodes[0].b_min_y), simd_hmin(qnodes[0].b_min_z));
        nodes[0].bounds.max = Vec3(simd_hmax(qnodes[0].b_max_x), simd_hmax(qnodes[0].b_max_y), simd_hmax(qnodes[0].b_max_z));
    }

private:
    PodVector<Vec3> cached_centers;
    PodVector<AABB> cached_boxes;

    int buildRecursive(int start, int end, PodVector<FlatBVHNode>& bin_nodes) {
        int count = end - start;
        int nodeIdx = (int)bin_nodes.size();
        bin_nodes.push_back({}); 
        
        AABB bounds;
        for (int i = start; i < end; ++i) bounds.expand(cached_boxes[triangle_indices[i]]);
        bin_nodes[nodeIdx].bounds = bounds;

        if (count <= MAX_OBJECTS_PER_LEAF) {
            bin_nodes[nodeIdx].leftOrFirst = start;
            bin_nodes[nodeIdx].count = count;
            return nodeIdx;
        }

        int best_axis = -1; float best_cost = MAX_FLOAT; float split_pos = 0.0f;
        Vec3 c_min(MAX_FLOAT, MAX_FLOAT, MAX_FLOAT);
        Vec3 c_max(MIN_FLOAT, MIN_FLOAT, MIN_FLOAT);
        for (int i = start; i < end; ++i) {
            Vec3 c = cached_centers[triangle_indices[i]];
            c_min = c_min.min(c); c_max = c_max.max(c);
        }
        Vec3 extent = c_max - c_min;

        for (int axis = 0; axis < 3; ++axis) {
            float len = (axis == 0 ? extent.x : (axis == 1 ? extent.y : extent.z));
            if (is_zero_f(len)) continue;

            constexpr int BINS = 16;
            Bin bins[BINS];
            float binScale = (float)BINS / len;
            float axisMin = (axis == 0 ? c_min.x : (axis == 1 ? c_min.y : c_min.z));

            for (int i = start; i < end; ++i) {
                int triIdx = triangle_indices[i];
                Vec3 c = cached_centers[triIdx];
                float val = (axis == 0 ? c.x : (axis == 1 ? c.y : c.z));
                int b = fast_clamp((int)((val - axisMin) * binScale), 0, BINS - 1);
                bins[b].count++;
                bins[b].bounds.expand(cached_boxes[triIdx]);
            }

            float leftArea[BINS - 1]; int leftCount[BINS - 1];
            AABB lBox; int lC = 0;
            for (int i = 0; i < BINS - 1; ++i) {
                lC += bins[i].count; lBox.expand(bins[i].bounds);
                leftCount[i] = lC; leftArea[i] = lBox.surfaceArea();
            }

            AABB rBox; int rC = 0;
            for (int i = BINS - 1; i > 0; --i) {
                rC += bins[i].count; rBox.expand(bins[i].bounds);
                int splitIdx = i - 1;
                if (leftCount[splitIdx] > 0 && rC > 0) {
                    float cost = leftArea[splitIdx] * (float)leftCount[splitIdx] + rBox.surfaceArea() * (float)rC;
                    if (cost < best_cost) { best_cost = cost; best_axis = axis; split_pos = axisMin + (i) * (len / BINS); }
                }
            }
        }

        if (best_axis == -1 || best_cost >= bounds.surfaceArea() * count) {
            bin_nodes[nodeIdx].leftOrFirst = start;
            bin_nodes[nodeIdx].count = count;
            return nodeIdx;
        }

        int lo = start, hi = end - 1;
        while (lo <= hi) {
            float c = (best_axis == 0 ? cached_centers[triangle_indices[lo]].x
                     : best_axis == 1 ? cached_centers[triangle_indices[lo]].y
                                      : cached_centers[triangle_indices[lo]].z);
            if (c < split_pos) ++lo;
            else {
                int tmp = triangle_indices[lo]; triangle_indices[lo] = triangle_indices[hi]; triangle_indices[hi] = tmp;
                --hi;
            }
        }
        
        int mid = lo;
        if (mid == start || mid == end) {
             bin_nodes[nodeIdx].leftOrFirst = start;
             bin_nodes[nodeIdx].count = count;
             return nodeIdx;
        }

        bin_nodes[nodeIdx].count = 0; 
        buildRecursive(start, mid, bin_nodes); 
        int rightChild = buildRecursive(mid, end, bin_nodes);
        bin_nodes[nodeIdx].leftOrFirst = rightChild; 
        return nodeIdx;
    }

    int collapseToQBVH(int bin_idx, const PodVector<FlatBVHNode>& bin_nodes) {
        int q_idx = (int)qnodes.size();
        qnodes.push_back({}); 
        
        const FlatBVHNode& bnode = bin_nodes[bin_idx];
        int b_children[4];
        int b_count = 0;
        
        if (bnode.count > 0) {
            b_children[0] = bin_idx; b_count = 1;
        } else {
            int L = bin_idx + 1;
            int R = bnode.leftOrFirst;
            
            if (bin_nodes[L].count == 0) {
                b_children[b_count++] = L + 1;
                b_children[b_count++] = bin_nodes[L].leftOrFirst;
            } else { b_children[b_count++] = L; }
            
            if (bin_nodes[R].count == 0) {
                b_children[b_count++] = R + 1;
                b_children[b_count++] = bin_nodes[R].leftOrFirst;
            } else { b_children[b_count++] = R; }
        }
        
        alignas(16) float mx[4] = {MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, MAX_FLOAT};
        alignas(16) float my[4] = {MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, MAX_FLOAT};
        alignas(16) float mz[4] = {MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, MAX_FLOAT};
        alignas(16) float Mx[4] = {MIN_FLOAT, MIN_FLOAT, MIN_FLOAT, MIN_FLOAT};
        alignas(16) float My[4] = {MIN_FLOAT, MIN_FLOAT, MIN_FLOAT, MIN_FLOAT};
        alignas(16) float Mz[4] = {MIN_FLOAT, MIN_FLOAT, MIN_FLOAT, MIN_FLOAT};
        
        int child_indices[4] = {0,0,0,0};
        int child_counts[4] = {-1,-1,-1,-1};
        
        for(int i = 0; i < b_count; ++i) {
            int c = b_children[i];
            const FlatBVHNode& cnode = bin_nodes[c];
            
            mx[i] = cnode.bounds.min.x; my[i] = cnode.bounds.min.y; mz[i] = cnode.bounds.min.z;
            Mx[i] = cnode.bounds.max.x; My[i] = cnode.bounds.max.y; Mz[i] = cnode.bounds.max.z;
            
            if (cnode.count > 0) { 
                child_indices[i] = cnode.leftOrFirst;
                child_counts[i] = cnode.count;
            } else { 
                child_indices[i] = collapseToQBVH(c, bin_nodes);
                child_counts[i] = 0; 
            }
        }
        
        QBVHNode& qnode = qnodes[q_idx];
        qnode.b_min_x = SimdFloat4(mx); qnode.b_min_y = SimdFloat4(my); qnode.b_min_z = SimdFloat4(mz);
        qnode.b_max_x = SimdFloat4(Mx); qnode.b_max_y = SimdFloat4(My); qnode.b_max_z = SimdFloat4(Mz);
        
        for(int i = 0; i < 4; i++) {
            qnode.children[i] = child_indices[i];
            qnode.num_packs[i] = child_counts[i];
        }
        
        return q_idx;
    }

public:
    FORCE_INLINE bool intersect(const Ray& ray, float& t_hit, const Triangle*& hit_tri, Vec3& hit_normal, float& hit_u, float& hit_v) const {
        static thread_local int tl_ctr = 0;
        bool do_sample = ((tl_ctr++) & kBvhSampleMask) == 0;
        PerfTimer t; if (do_sample) t.start();

        if (qnodes.empty()) return false;
        
        int stack[kBvhStackDepth];
        int ptr = 0;
        stack[ptr++] = 0; 

        bool hit_something = false;
        const QBVHNode* nodePtr = qnodes.data();
        
        SimdFloat4 org_x(ray.origin.x), org_y(ray.origin.y), org_z(ray.origin.z);
        SimdFloat4 inv_x(ray.inv_direction.x), inv_y(ray.inv_direction.y), inv_z(ray.inv_direction.z);
        
        alignas(16) float t_arr[4];
        alignas(16) float u_arr[4];
        alignas(16) float v_arr[4];

        while(ptr > 0) {
            const QBVHNode& qnode = nodePtr[stack[--ptr]];
            
            SimdFloat4 t0x = (qnode.b_min_x - org_x) * inv_x;
            SimdFloat4 t1x = (qnode.b_max_x - org_x) * inv_x;
            SimdFloat4 t0y = (qnode.b_min_y - org_y) * inv_y;
            SimdFloat4 t1y = (qnode.b_max_y - org_y) * inv_y;
            SimdFloat4 t0z = (qnode.b_min_z - org_z) * inv_z;
            SimdFloat4 t1z = (qnode.b_max_z - org_z) * inv_z;
            
            SimdFloat4 tminx = simd_min(t0x, t1x); SimdFloat4 tmaxx = simd_max(t0x, t1x);
            SimdFloat4 tminy = simd_min(t0y, t1y); SimdFloat4 tmaxy = simd_max(t0y, t1y);
            SimdFloat4 tminz = simd_min(t0z, t1z); SimdFloat4 tmaxz = simd_max(t0z, t1z);
            
            SimdFloat4 t_enter = simd_max(simd_max(tminx, tminy), simd_max(tminz, SimdFloat4(0.0f)));
            SimdFloat4 t_exit  = simd_min(simd_min(tmaxx, tmaxy), tmaxz);
            SimdFloat4 t_hit_simd(t_hit);
            
            SimdFloat4 hit_mask = (t_enter <= t_exit) & (t_enter < t_hit_simd);
            int mask = simd_movemask(hit_mask);
            
            if (mask == 0) continue;

            _mm_store_ps(t_arr, t_enter.m);

            int hit_indices[4];
            int hc = 0;
            int m = mask;
            while(m) {
#if defined(_MSC_VER)
                unsigned long idx; _BitScanForward(&idx, m);
#else
                int idx = __builtin_ctz(m);
#endif
                hit_indices[hc++] = idx;
                m &= m - 1; 
            }

            if (hc == 2) {
                if (t_arr[hit_indices[0]] < t_arr[hit_indices[1]]) {
                    int tmp = hit_indices[0]; hit_indices[0] = hit_indices[1]; hit_indices[1] = tmp;
                }
            } else if (hc > 2) {
                for(int i=1; i<hc; ++i) {
                    int key = hit_indices[i];
                    float key_t = t_arr[key];
                    int j = i - 1;
                    while(j >= 0 && t_arr[hit_indices[j]] < key_t) {
                        hit_indices[j+1] = hit_indices[j];
                        j--;
                    }
                    hit_indices[j+1] = key;
                }
            }

            for (int i = 0; i < hc; ++i) {
                int c = hit_indices[i];
                int n_packs = qnode.num_packs[c];
                
                if (n_packs > 0) { 
                    int end = qnode.children[c] + n_packs;
                    for(int p = qnode.children[c]; p < end; ++p) {
                        const TrianglePack& pack = packed_triangles[p];
                        SimdFloat4 t_max_s(t_hit);
                        SimdFloat4 out_u, out_v;
                        
                        SimdFloat4 t_res = pack.intersect(ray, t_max_s, out_u, out_v);
                        int p_mask = simd_movemask(t_res < t_max_s);
                        if (p_mask == 0) continue;

                        _mm_store_ps(t_arr, t_res.m);
                        _mm_store_ps(u_arr, out_u.m);
                        _mm_store_ps(v_arr, out_v.m);
                        
                        while (p_mask != 0) {
#if defined(_MSC_VER)
                            unsigned long k; _BitScanForward(&k, p_mask);
#else
                            int k = __builtin_ctz(p_mask);
#endif
                            p_mask &= ~(1 << k);
                            if (t_arr[k] < t_hit) {
                                t_hit = t_arr[k];
                                int triIdx = pack.indices[k];
                                if (triIdx >= 0) {
                                    const Triangle& tri = (*source_triangles)[triIdx];
                                    float b1 = u_arr[k]; float b2 = v_arr[k]; float b0 = 1.0f - b1 - b2;
                                    hit_normal = (tri.n0 * b0 + tri.n1 * b1 + tri.n2 * b2).normalize();
                                    hit_u = tri.uv0.u * b0 + tri.uv1.u * b1 + tri.uv2.u * b2;
                                    hit_v = tri.uv0.v * b0 + tri.uv1.v * b1 + tri.uv2.v * b2;
                                    hit_tri = &tri;
                                    hit_something = true;
                                }
                            }
                        }
                    }
                } else if (n_packs == 0) { 
                    if (ptr < kBvhStackSafeLimit) {
                        int child_idx = qnode.children[c];
                        _mm_prefetch((const char*)&nodePtr[child_idx], _MM_HINT_T0);
                        stack[ptr++] = child_idx;
                    }
                }
            }
        }

        if (do_sample) g_metrics.addBvh(t.getElapsedMs() * kBvhMetricsScale);
        return hit_something;
    }
    
    FORCE_INLINE bool intersectsAny(const Ray& ray, float max_t) const {
        static thread_local int tl_ctr_any = 0;
        bool do_sample = ((tl_ctr_any++) & kBvhSampleMask) == 0;
        PerfTimer t; if (do_sample) t.start();

        if (qnodes.empty()) return false;
        
        int stack[kBvhStackDepth];
        int ptr = 0; stack[ptr++] = 0;
        const QBVHNode* nodePtr = qnodes.data();

        SimdFloat4 org_x(ray.origin.x), org_y(ray.origin.y), org_z(ray.origin.z);
        SimdFloat4 inv_x(ray.inv_direction.x), inv_y(ray.inv_direction.y), inv_z(ray.inv_direction.z);
        SimdFloat4 t_max_simd(max_t);

        while(ptr > 0) {
            const QBVHNode& qnode = nodePtr[stack[--ptr]];
            
            SimdFloat4 t0x = (qnode.b_min_x - org_x) * inv_x; SimdFloat4 t1x = (qnode.b_max_x - org_x) * inv_x;
            SimdFloat4 t0y = (qnode.b_min_y - org_y) * inv_y; SimdFloat4 t1y = (qnode.b_max_y - org_y) * inv_y;
            SimdFloat4 t0z = (qnode.b_min_z - org_z) * inv_z; SimdFloat4 t1z = (qnode.b_max_z - org_z) * inv_z;
            
            SimdFloat4 t_enter = simd_max(simd_max(simd_min(t0x, t1x), simd_min(t0y, t1y)), simd_max(simd_min(t0z, t1z), SimdFloat4(0.0f)));
            SimdFloat4 t_exit  = simd_min(simd_min(simd_max(t0x, t1x), simd_max(t0y, t1y)), simd_max(t0z, t1z));
            
            int mask = simd_movemask((t_enter <= t_exit) & (t_enter < t_max_simd));
            
            while(mask) {
#if defined(_MSC_VER)
                unsigned long i; _BitScanForward(&i, mask);
#else
                int i = __builtin_ctz(mask);
#endif
                mask &= mask - 1; 

                if (qnode.num_packs[i] > 0) {
                    int end = qnode.children[i] + qnode.num_packs[i];
                    for(int p = qnode.children[i]; p < end; ++p) {
                        SimdFloat4 dummy_u, dummy_v;
                        if (simd_movemask(packed_triangles[p].intersect(ray, t_max_simd, dummy_u, dummy_v) < t_max_simd) != 0) {
                            if (do_sample) g_metrics.addBvh(t.getElapsedMs() * kBvhMetricsScale);
                            return true; 
                        }
                    }
                } else if (qnode.num_packs[i] == 0) {
                    if (ptr < kBvhStackSafeLimit) {
                        int child_idx = qnode.children[i];
                        _mm_prefetch((const char*)&nodePtr[child_idx], _MM_HINT_T0);
                        stack[ptr++] = child_idx;
                    }
                }
            }
        }

        if (do_sample) g_metrics.addBvh(t.getElapsedMs() * kBvhMetricsScale);
        return false;
    }

    void queryOverlap(const AABB& box, PodVector<const Triangle*>& out_tris) const {
        if (qnodes.empty()) return;
        int stack[kBvhStackDepth]; int ptr = 0; stack[ptr++] = 0;
        const QBVHNode* nodePtr = qnodes.data();

        while(ptr > 0) {
            const QBVHNode& qnode = nodePtr[stack[--ptr]];
            alignas(16) float mx[4], my[4], mz[4];
            alignas(16) float Mx[4], My[4], Mz[4];
            _mm_store_ps(mx, qnode.b_min_x.m); _mm_store_ps(my, qnode.b_min_y.m); _mm_store_ps(mz, qnode.b_min_z.m);
            _mm_store_ps(Mx, qnode.b_max_x.m); _mm_store_ps(My, qnode.b_max_y.m); _mm_store_ps(Mz, qnode.b_max_z.m);

            for (int i = 0; i < 4; ++i) {
                if (qnode.num_packs[i] == -1) continue;
                AABB childBox(Vec3(mx[i], my[i], mz[i]), Vec3(Mx[i], My[i], Mz[i]));
                if (childBox.overlaps(box)) {
                    if (qnode.num_packs[i] > 0) {
                        int end = qnode.children[i] + qnode.num_packs[i];
                        for(int p = qnode.children[i]; p < end; ++p) {
                            const TrianglePack& pack = packed_triangles[p];
                            for(int k=0; k<pack.count; ++k) {
                                int triIdx = pack.indices[k];
                                const Triangle& tri = (*source_triangles)[triIdx];
                                if (tri.getAABB().overlaps(box)) out_tris.push_back(&tri);
                            }
                        }
                    } else {
                        if (ptr < kBvhStackSafeLimit) stack[ptr++] = qnode.children[i];
                    }
                }
            }
        }
    }
};

#endif