#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "engine_config.h"
#include "vector.h"
#include "ray.h"
#include "aabb.h"
#include "math_utils.h"
#include <cstdint>

struct alignas(16) Triangle {
    Vec3 v0; 
    Vec3 edge1; 
    Vec3 edge2; 
    Vec3 n0; 
    Vec3 n1; 
    Vec3 n2;
    Vec2 uv0; 
    Vec2 uv1; 
    Vec2 uv2;
    Vec3 face_normal;
    uint32_t material_id; 
    uint32_t padding;

    Triangle() = default;

    Triangle(const Vec3& _v0, const Vec3& _v1, const Vec3& _v2,
             const Vec2& _uv0, const Vec2& _uv1, const Vec2& _uv2,
             uint32_t _mat_id)
        : v0(_v0), edge1(_v1 - _v0), edge2(_v2 - _v0),
          n0(0,0,0), n1(0,0,0), n2(0,0,0),
          face_normal(0,0,0),
          uv0(_uv0), uv1(_uv1), uv2(_uv2),
          material_id(_mat_id), padding(0)
    {
        Vec3 face_n = edge1.cross(edge2).normalize();
        n0 = face_n; n1 = face_n; n2 = face_n;
        face_normal = face_n;
    }

    FORCE_INLINE Vec3 getV1() const { return v0 + edge1; }
    FORCE_INLINE Vec3 getV2() const { return v0 + edge2; }

    FORCE_INLINE bool intersect(const Ray& ray, float& t_max, Vec3& out_normal, float& u, float& v) const {
        const Vec3 pvec = ray.direction.cross(edge2);
        const float det = edge1.dot(pvec);
        if (is_zero_f(det)) return false;

        const float invDet = 1.0f / det;
        const Vec3 tvec = ray.origin - v0;
        const float u_val = tvec.dot(pvec) * invDet;

        if (u_val < 0.0f || u_val > 1.0f) return false;

        const Vec3 qvec = tvec.cross(edge1);
        const float v_val = ray.direction.dot(qvec) * invDet;

        if (v_val < 0.0f || (u_val + v_val) > 1.0f) return false;

        const float t_val = edge2.dot(qvec) * invDet;
        
        if (t_val > EPSILON_ABS && t_val < t_max) {
            t_max = t_val;
            u = u_val;
            v = v_val;
            out_normal = face_normal;
            return true;
        }
        return false;
    }

    FORCE_INLINE AABB getAABB() const {
        AABB box;
        box.expand(v0);
        box.expand(v0 + edge1);
        box.expand(v0 + edge2);
        const Vec3 paddingVec(EPSILON_ABS, EPSILON_ABS, EPSILON_ABS);
        box.min = box.min - paddingVec; 
        box.max = box.max + paddingVec;
        return box;
    }

    FORCE_INLINE Vec3 getCentroid() const {
        return (v0 * (1.0f / kOneThird) + edge1 + edge2) * kOneThird;
    }
    
    FORCE_INLINE Vec3 getFaceNormal() const { return face_normal; }
};

struct alignas(32) TrianglePack {
    SimdFloat4 v0_x, v0_y, v0_z;
    SimdFloat4 e1_x, e1_y, e1_z;
    SimdFloat4 e2_x, e2_y, e2_z;
    int indices[4]; 
    int count = 0;

    void set(int lane, const Triangle& t, int original_index) {
        float* px = (float*)&v0_x; px[lane] = t.v0.x;
        float* py = (float*)&v0_y; py[lane] = t.v0.y;
        float* pz = (float*)&v0_z; pz[lane] = t.v0.z;

        px = (float*)&e1_x; px[lane] = t.edge1.x;
        py = (float*)&e1_y; py[lane] = t.edge1.y;
        pz = (float*)&e1_z; pz[lane] = t.edge1.z;

        px = (float*)&e2_x; px[lane] = t.edge2.x;
        py = (float*)&e2_y; py[lane] = t.edge2.y;
        pz = (float*)&e2_z; pz[lane] = t.edge2.z;
        
        indices[lane] = original_index;
    }

    FORCE_INLINE SimdFloat4 intersect(const Ray& ray, const SimdFloat4& t_max_curr, SimdFloat4& out_u, SimdFloat4& out_v) const {
        SimdFloat4 ro_x(ray.origin.x), ro_y(ray.origin.y), ro_z(ray.origin.z);
        SimdFloat4 rd_x(ray.direction.x), rd_y(ray.direction.y), rd_z(ray.direction.z);

        SimdFloat4 pvec_x = rd_y * e2_z - rd_z * e2_y;
        SimdFloat4 pvec_y = rd_z * e2_x - rd_x * e2_z;
        SimdFloat4 pvec_z = rd_x * e2_y - rd_y * e2_x;

        SimdFloat4 det = e1_x * pvec_x + e1_y * pvec_y + e1_z * pvec_z;
        
        SimdFloat4 abs_det = det & SimdFloat4(uint_as_float(0x7FFFFFFF));
        SimdFloat4 det_mask = abs_det > SimdFloat4(EPSILON_ABS);
        if (simd_movemask(det_mask) == 0) return SimdFloat4(MAX_FLOAT);

        __m128 _r   = _mm_rcp_ps(det.m);
        SimdFloat4 invDet(_mm_mul_ps(_r, _mm_sub_ps(_mm_set1_ps(2.0f),
                                                     _mm_mul_ps(det.m, _r))));

        SimdFloat4 tvec_x = ro_x - v0_x;
        SimdFloat4 tvec_y = ro_y - v0_y;
        SimdFloat4 tvec_z = ro_z - v0_z;

        SimdFloat4 u = (tvec_x * pvec_x + tvec_y * pvec_y + tvec_z * pvec_z) * invDet;
        SimdFloat4 u_mask = (u >= SimdFloat4(0.0f)) & (u <= SimdFloat4(1.0f));
        
        SimdFloat4 active_mask = det_mask & u_mask;
        if (simd_movemask(active_mask) == 0) return SimdFloat4(MAX_FLOAT);

        SimdFloat4 qvec_x = tvec_y * e1_z - tvec_z * e1_y;
        SimdFloat4 qvec_y = tvec_z * e1_x - tvec_x * e1_z;
        SimdFloat4 qvec_z = tvec_x * e1_y - tvec_y * e1_x;

        SimdFloat4 v = (rd_x * qvec_x + rd_y * qvec_y + rd_z * qvec_z) * invDet;
        SimdFloat4 v_mask = (v >= SimdFloat4(0.0f)) & ((u + v) <= SimdFloat4(1.0f));

        active_mask = active_mask & v_mask;
        if (simd_movemask(active_mask) == 0) return SimdFloat4(MAX_FLOAT);

        SimdFloat4 t_val = (e2_x * qvec_x + e2_y * qvec_y + e2_z * qvec_z) * invDet;
        SimdFloat4 t_mask = (t_val > SimdFloat4(EPSILON_ABS)) & (t_val < t_max_curr);

        active_mask = active_mask & t_mask;
        if (simd_movemask(active_mask) == 0) return SimdFloat4(MAX_FLOAT);

        out_u = u;
        out_v = v;
        return simd_blend(active_mask, t_val, SimdFloat4(MAX_FLOAT));
    }
};

#endif