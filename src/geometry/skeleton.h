#pragma once

#include "quat.h"
#include "matrix.h"
#include "vector.h"
#include "triangle.h"
#include "str.h"

struct SkinVertex {
    float bone_weights[MAX_BONE_INFLUENCES] = {1.f, 0.f, 0.f, 0.f};
    int   bone_indices[MAX_BONE_INFLUENCES] = {0,    0,   0,   0  };
    int   count = 1;
};

struct Joint {
    StrBuf<64> name;

    int parent_index = kNoParent;
    Mat4 inverse_bind_pose;
    Mat4 bind_pose;

    Quat  local_rotation    = Quat::identity();
    Vec3  local_translation = Vec3(0.f, 0.f, 0.f);
    float local_scale       = 1.f;

    Mat4 world_transform;
    Mat4 skin_matrix;
};
struct Skeleton {
    PodVector<Joint> joints;

    int addJoint(const char* name, int parent_index,
                 const Mat4& bind_pose_world,
                 const Mat4* custom_inverse_bind = nullptr)
    {
        Joint j;
        j.name              = name;
        j.parent_index      = parent_index;
        j.bind_pose         = bind_pose_world;
        
        if (custom_inverse_bind) j.inverse_bind_pose = *custom_inverse_bind;
        else j.inverse_bind_pose = Mat4::inverse(bind_pose_world);

        j.world_transform   = bind_pose_world;
        j.skin_matrix       = Mat4::identity();

        if (parent_index >= 0 && parent_index < (int)joints.size()) {
            Mat4 parent_inv  = Mat4::inverse(joints[parent_index].bind_pose);
            Mat4 local_bind  = parent_inv * bind_pose_world;

            alignas(16) float r0[4], r1[4], r2[4];
            _mm_store_ps(r0, local_bind.rows[0]);
            _mm_store_ps(r1, local_bind.rows[1]);
            _mm_store_ps(r2, local_bind.rows[2]);
            j.local_translation = Vec3(r0[3], r1[3], r2[3]);
            j.local_rotation    = Quat::fromMat4(local_bind).normalize();
        }

        joints.push_back(j);
        return (int)joints.size() - 1;
    }

    void computeWorldTransforms() {
        for (int i = 0; i < (int)joints.size(); ++i) {
            Joint& j = joints[i];

            Mat4 local = j.local_rotation.toMat4();

            if (j.local_scale != 1.f) {
                __m128 s = _mm_set1_ps(j.local_scale);
                local.rows[0] = _mm_mul_ps(local.rows[0], s);
                local.rows[1] = _mm_mul_ps(local.rows[1], s);
                local.rows[2] = _mm_mul_ps(local.rows[2], s);
            }

            alignas(16) float r0[4], r1[4], r2[4];
            _mm_store_ps(r0, local.rows[0]);
            _mm_store_ps(r1, local.rows[1]);
            _mm_store_ps(r2, local.rows[2]);
            r0[3] = j.local_translation.x;
            r1[3] = j.local_translation.y;
            r2[3] = j.local_translation.z;
            local.rows[0] = _mm_load_ps(r0);
            local.rows[1] = _mm_load_ps(r1);
            local.rows[2] = _mm_load_ps(r2);

            if (j.parent_index < 0) {
                j.world_transform = local;
            } else {
                j.world_transform = joints[j.parent_index].world_transform * local;
            }

            j.skin_matrix = j.world_transform * j.inverse_bind_pose;
        }
    }

    void skin(const PodVector<Triangle>& src,
                    PodVector<Triangle>& dst,
              const PodVector<SkinVertex>& skin_data) const
    {
        const int n = (int)src.size();
        if (dst.size() != (size_t)n) dst.resize(n);

        for (int ti = 0; ti < n; ++ti) {
            const Triangle& st = src[ti];
            Triangle&       dt = dst[ti];

            dt.uv0          = st.uv0;
            dt.uv1          = st.uv1;
            dt.uv2          = st.uv2;
            dt.material_id  = st.material_id;

            const Vec3 v1_ws = st.v0 + st.edge1;
            const Vec3 v2_ws = st.v0 + st.edge2;

            const Vec3 sv0 = skinPoint (ti*3+0, st.v0,    skin_data);
            const Vec3 sv1 = skinPoint (ti*3+1, v1_ws,    skin_data);
            const Vec3 sv2 = skinPoint (ti*3+2, v2_ws,    skin_data);
            const Vec3 sn0 = skinNormal(ti*3+0, st.n0,    skin_data);
            const Vec3 sn1 = skinNormal(ti*3+1, st.n1,    skin_data);
            const Vec3 sn2 = skinNormal(ti*3+2, st.n2,    skin_data);

            dt.v0          = sv0;
            dt.edge1       = sv1 - sv0;
            dt.edge2       = sv2 - sv0;
            dt.n0          = sn0;
            dt.n1          = sn1;
            dt.n2          = sn2;
            dt.face_normal = dt.edge1.cross(dt.edge2).normalize();
        }
    }

private:
    FORCE_INLINE Vec3 skinPoint(int vi, const Vec3& p,
                                const PodVector<SkinVertex>& sd) const
    {
        if (vi >= (int)sd.size()) return p;
        const SkinVertex& sv = sd[vi];
        __m128 acc = _mm_setzero_ps();
        for (int k = 0; k < sv.count; ++k) {
            const int   bi = sv.bone_indices[k];
            const float bw = sv.bone_weights[k];
            if (bi < 0 || bi >= (int)joints.size()) continue;
            const Vec3 xp = joints[bi].skin_matrix.transformPoint(p);
            acc = _mm_add_ps(acc, _mm_mul_ps(xp.load(), _mm_set1_ps(bw)));
        }
        return Vec3(acc);
    }

    FORCE_INLINE Vec3 skinNormal(int vi, const Vec3& n,
                                 const PodVector<SkinVertex>& sd) const
    {
        if (vi >= (int)sd.size()) return n;
        const SkinVertex& sv = sd[vi];
        __m128 acc = _mm_setzero_ps();
        for (int k = 0; k < sv.count; ++k) {
            const int   bi = sv.bone_indices[k];
            const float bw = sv.bone_weights[k];
            if (bi < 0 || bi >= (int)joints.size()) continue;
            const Vec3 xn = joints[bi].skin_matrix.transformDir(n);
            acc = _mm_add_ps(acc, _mm_mul_ps(xn.load(), _mm_set1_ps(bw)));
        }
        Vec3 result(acc);
        return result.normalize();
    }
};