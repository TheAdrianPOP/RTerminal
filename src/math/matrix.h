#ifndef MATRIX_H
#define MATRIX_H

#include "vector.h"
#include <immintrin.h>

struct alignas(16) Mat4 {
    __m128 rows[4];

    FORCE_INLINE Mat4() {
        rows[0] = _mm_set_ps(0, 0, 0, 1);
        rows[1] = _mm_set_ps(0, 0, 1, 0);
        rows[2] = _mm_set_ps(0, 1, 0, 0);
        rows[3] = _mm_set_ps(1, 0, 0, 0);
    }

    static FORCE_INLINE Mat4 identity() { return Mat4(); }

    FORCE_INLINE Mat4 operator*(const Mat4& b) const {
        Mat4 res;
        for (int i = 0; i < 4; i++) {
            __m128 row = rows[i];
            
            __m128 splatX = _mm_shuffle_ps(row, row, _MM_SHUFFLE(0, 0, 0, 0));
            __m128 splatY = _mm_shuffle_ps(row, row, _MM_SHUFFLE(1, 1, 1, 1));
            __m128 splatZ = _mm_shuffle_ps(row, row, _MM_SHUFFLE(2, 2, 2, 2));
            __m128 splatW = _mm_shuffle_ps(row, row, _MM_SHUFFLE(3, 3, 3, 3));
            
            __m128 r = _mm_mul_ps(splatX, b.rows[0]);
            r = _mm_add_ps(r, _mm_mul_ps(splatY, b.rows[1]));
            r = _mm_add_ps(r, _mm_mul_ps(splatZ, b.rows[2]));
            r = _mm_add_ps(r, _mm_mul_ps(splatW, b.rows[3]));
            
            res.rows[i] = r;
        }
        return res;
    }

    FORCE_INLINE Vec3 transformPoint(const Vec3& v) const {
#ifdef __SSE4_1__
        __m128 v4 = _mm_insert_ps(v.load(), _mm_set_ss(1.0f), 0x30); 
        float x = _mm_cvtss_f32(_mm_dp_ps(rows[0], v4, 0xF1));
        float y = _mm_cvtss_f32(_mm_dp_ps(rows[1], v4, 0xF1));
        float z = _mm_cvtss_f32(_mm_dp_ps(rows[2], v4, 0xF1));
        return Vec3(x, y, z);
#else
        auto dot3_add_w = [](const __m128& row, const __m128& vec) {
            __m128 m = _mm_mul_ps(row, vec); 
            
            __m128 rw = _mm_shuffle_ps(row, row, _MM_SHUFFLE(3, 3, 3, 3));
            
            __m128 sum = _mm_add_ss(m, _mm_shuffle_ps(m, m, _MM_SHUFFLE(1, 1, 1, 1)));
            sum = _mm_add_ss(sum, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 2, 2, 2)));
            sum = _mm_add_ss(sum, rw);
            return _mm_cvtss_f32(sum);
        };

        float x = dot3_add_w(rows[0], v.load());
        float y = dot3_add_w(rows[1], v.load());
        float z = dot3_add_w(rows[2], v.load());
        return Vec3(x, y, z);
#endif
    }

    FORCE_INLINE Vec3 transformDir(const Vec3& v) const {
#ifdef __SSE4_1__
        float x = _mm_cvtss_f32(_mm_dp_ps(rows[0], v.load(), 0x71));
        float y = _mm_cvtss_f32(_mm_dp_ps(rows[1], v.load(), 0x71));
        float z = _mm_cvtss_f32(_mm_dp_ps(rows[2], v.load(), 0x71));
        return Vec3(x, y, z);
#else
        auto dot3 = [](const __m128& row, const __m128& vec) {
            __m128 m = _mm_mul_ps(row, vec);
            __m128 sum = _mm_add_ss(m, _mm_shuffle_ps(m, m, _MM_SHUFFLE(1, 1, 1, 1)));
            sum = _mm_add_ss(sum, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2, 2, 2, 2)));
            return _mm_cvtss_f32(sum);
        };
        
        float x = dot3(rows[0], v.load());
        float y = dot3(rows[1], v.load());
        float z = dot3(rows[2], v.load());
        return Vec3(x, y, z);
#endif
    }

    static FORCE_INLINE Mat4 translation(const Vec3& v) {
        Mat4 m;
        m.rows[0] = _mm_set_ps(v.x, 0, 0, 1);
        m.rows[1] = _mm_set_ps(v.y, 0, 1, 0);
        m.rows[2] = _mm_set_ps(v.z, 1, 0, 0);
        m.rows[3] = _mm_set_ps(1, 0, 0, 0);
        return m;
    }

    static FORCE_INLINE Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = (target - eye).normalize();
        Vec3 r = f.cross(up).normalize();
        Vec3 u = r.cross(f);

        Mat4 m;
        m.rows[0] = _mm_set_ps(0.0f, r.z, r.y, r.x);
        m.rows[1] = _mm_set_ps(0.0f, u.z, u.y, u.x);
        m.rows[2] = _mm_set_ps(0.0f, f.z, f.y, f.x);
        m.rows[3] = _mm_set_ps(1.0f, -eye.z, -eye.y, -eye.x); 
        return m;
    }

    static FORCE_INLINE Mat4 rigidInverse(const Mat4& m) {
        alignas(16) float r0[4], r1[4], r2[4];
        _mm_store_ps(r0, m.rows[0]); _mm_store_ps(r1, m.rows[1]); _mm_store_ps(r2, m.rows[2]);
        const float tx = r0[3], ty = r1[3], tz = r2[3];
        const float itx = -(r0[0]*tx + r1[0]*ty + r2[0]*tz);
        const float ity = -(r0[1]*tx + r1[1]*ty + r2[1]*tz);
        const float itz = -(r0[2]*tx + r1[2]*ty + r2[2]*tz);
        Mat4 result;
        result.rows[0] = _mm_set_ps(itx, r2[0], r1[0], r0[0]);
        result.rows[1] = _mm_set_ps(ity, r2[1], r1[1], r0[1]);
        result.rows[2] = _mm_set_ps(itz, r2[2], r1[2], r0[2]);
        result.rows[3] = _mm_set_ps(1.f, 0.f,   0.f,   0.f  );
        return result;
    }

    static FORCE_INLINE Mat4 inverse(const Mat4& m) {
        alignas(16) float mat[16];
        _mm_store_ps(&mat[0], m.rows[0]); _mm_store_ps(&mat[4], m.rows[1]);
        _mm_store_ps(&mat[8], m.rows[2]); _mm_store_ps(&mat[12], m.rows[3]);

        float res[16];
        float a0 = mat[0]*mat[5] - mat[1]*mat[4]; float a1 = mat[0]*mat[6] - mat[2]*mat[4];
        float a2 = mat[0]*mat[7] - mat[3]*mat[4]; float a3 = mat[1]*mat[6] - mat[2]*mat[5];
        float a4 = mat[1]*mat[7] - mat[3]*mat[5]; float a5 = mat[2]*mat[7] - mat[3]*mat[6];
        float b0 = mat[8]*mat[13] - mat[9]*mat[12]; float b1 = mat[8]*mat[14] - mat[10]*mat[12];
        float b2 = mat[8]*mat[15] - mat[11]*mat[12]; float b3 = mat[9]*mat[14] - mat[10]*mat[13];
        float b4 = mat[9]*mat[15] - mat[11]*mat[13]; float b5 = mat[10]*mat[15] - mat[11]*mat[14];
        float det = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
        if (is_zero_f(det)) return m;
        float invDet = 1.0f / det;
        res[0] = ( mat[5]*b5 -  mat[6]*b4 +  mat[7]*b3) * invDet; res[1] = (-mat[1]*b5 +  mat[2]*b4 -  mat[3]*b3) * invDet;
        res[2] = ( mat[13]*a5 - mat[14]*a4 + mat[15]*a3) * invDet; res[3] = (-mat[9]*a5 + mat[10]*a4 - mat[11]*a3) * invDet;
        res[4] = (-mat[4]*b5 +  mat[6]*b2 -  mat[7]*b1) * invDet; res[5] = ( mat[0]*b5 -  mat[2]*b2 +  mat[3]*b1) * invDet;
        res[6] = (-mat[12]*a5 + mat[14]*a2 - mat[15]*a1) * invDet; res[7] = ( mat[8]*a5 - mat[10]*a2 + mat[11]*a1) * invDet;
        res[8] = ( mat[4]*b4 -  mat[5]*b2 +  mat[7]*b0) * invDet; res[9] = (-mat[0]*b4 +  mat[1]*b2 -  mat[3]*b0) * invDet;
        res[10] = ( mat[12]*a4 - mat[13]*a2 + mat[15]*a0) * invDet; res[11] = (-mat[8]*a4 + mat[9]*a2 - mat[11]*a0) * invDet;
        res[12] = (-mat[4]*b3 +  mat[5]*b1 -  mat[6]*b0) * invDet; res[13] = ( mat[0]*b3 -  mat[1]*b1 +  mat[2]*b0) * invDet;
        res[14] = (-mat[12]*a3 + mat[13]*a1 - mat[14]*a0) * invDet; res[15] = ( mat[8]*a3 - mat[9]*a1 + mat[10]*a0) * invDet;

        Mat4 out;
        out.rows[0] = _mm_set_ps(res[3], res[2], res[1], res[0]); out.rows[1] = _mm_set_ps(res[7], res[6], res[5], res[4]);
        out.rows[2] = _mm_set_ps(res[11], res[10], res[9], res[8]); out.rows[3] = _mm_set_ps(res[15], res[14], res[13], res[12]);
        return out;
    }

    FORCE_INLINE void transpose() {
        _MM_TRANSPOSE4_PS(rows[0], rows[1], rows[2], rows[3]);
    }

    static FORCE_INLINE Mat4 scale(float s) {
        Mat4 m;
        m.rows[0] = _mm_set_ps(0.f, 0.f, 0.f,  s);
        m.rows[1] = _mm_set_ps(0.f, 0.f,  s, 0.f);
        m.rows[2] = _mm_set_ps(0.f,  s, 0.f, 0.f);
        m.rows[3] = _mm_set_ps(1.f, 0.f, 0.f, 0.f);
        return m;
    }

    static FORCE_INLINE Mat4 scale(float sx, float sy, float sz) {
        Mat4 m;
        m.rows[0] = _mm_set_ps(0.f,  0.f,  0.f,  sx);
        m.rows[1] = _mm_set_ps(0.f,  0.f,  sy, 0.f);
        m.rows[2] = _mm_set_ps(0.f,  sz, 0.f, 0.f);
        m.rows[3] = _mm_set_ps(1.f, 0.f, 0.f, 0.f);
        return m;
    }

    static FORCE_INLINE Mat4 rotationX(float a) {
        const float c = fast_cos(a), s = fast_sin(a);
        Mat4 m;
        m.rows[0] = _mm_set_ps(0.f,  0.f,  0.f, 1.f);
        m.rows[1] = _mm_set_ps(0.f,   -s,    c, 0.f);
        m.rows[2] = _mm_set_ps(0.f,    c,    s, 0.f);
        m.rows[3] = _mm_set_ps(1.f,  0.f,  0.f, 0.f);
        return m;
    }

    static FORCE_INLINE Mat4 rotationY(float a) {
        const float c = fast_cos(a), s = fast_sin(a);
        Mat4 m;
        m.rows[0] = _mm_set_ps(0.f,    s, 0.f,   c);
        m.rows[1] = _mm_set_ps(0.f,  0.f, 1.f, 0.f);
        m.rows[2] = _mm_set_ps(0.f,    c, 0.f,  -s);
        m.rows[3] = _mm_set_ps(1.f,  0.f, 0.f, 0.f);
        return m;
    }

    static FORCE_INLINE Mat4 rotationZ(float a) {
        const float c = fast_cos(a), s = fast_sin(a);
        Mat4 m;
        m.rows[0] = _mm_set_ps(0.f, 0.f,  -s,   c);
        m.rows[1] = _mm_set_ps(0.f, 0.f,   c,   s);
        m.rows[2] = _mm_set_ps(0.f, 1.f, 0.f, 0.f);
        m.rows[3] = _mm_set_ps(1.f, 0.f, 0.f, 0.f);
        return m;
    }

    static FORCE_INLINE Mat4 TRS(const Vec3& pos, float yaw, float pitch, float s = 1.f) {
        return translation(pos) * rotationY(yaw) * rotationX(pitch) * scale(s);
    }
};

#endif // MATRIX_H