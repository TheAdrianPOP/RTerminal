#pragma once
#ifndef QUAT_H
#define QUAT_H

#include "math_utils.h"
#include "vector.h"
#include "matrix.h"

struct alignas(16) Quat {
    float x, y, z, w;

    FORCE_INLINE Quat() { store(_mm_set_ps(1.f, 0.f, 0.f, 0.f)); }
    FORCE_INLINE Quat(float x_, float y_, float z_, float w_) { store(_mm_set_ps(w_, z_, y_, x_)); }
    FORCE_INLINE explicit Quat(__m128 m) { store(m); }

    FORCE_INLINE __m128 load() const { return _mm_load_ps(&x); }
    FORCE_INLINE void store(__m128 m) { _mm_store_ps(&x, m); }

    static FORCE_INLINE Quat identity() { return Quat(0.f, 0.f, 0.f, 1.f); }

    static FORCE_INLINE Quat fromAxisAngle(const Vec3& axis, float angle) {
        const Vec3  n   = axis.normalize();
        const float s   = fast_sin(angle * 0.5f);
        const float c   = fast_cos(angle * 0.5f);
        return Quat(n.x * s, n.y * s, n.z * s, c);
    }

    static FORCE_INLINE Quat fromEuler(float yaw, float pitch, float roll = 0.f) {
        const float cy = fast_cos(yaw   * 0.5f), sy = fast_sin(yaw   * 0.5f);
        const float cp = fast_cos(pitch * 0.5f), sp = fast_sin(pitch * 0.5f);
        const float cr = fast_cos(roll  * 0.5f), sr = fast_sin(roll  * 0.5f);
        return Quat(
            cy*sp*cr + sy*cp*sr,
            sy*cp*cr - cy*sp*sr,
            cy*cp*sr - sy*sp*cr,
            cy*cp*cr + sy*sp*sr
        );
    }

    static Quat fromMat4(const Mat4& m) {
        alignas(16) float r0[4], r1[4], r2[4];
        _mm_store_ps(r0, m.rows[0]);
        _mm_store_ps(r1, m.rows[1]);
        _mm_store_ps(r2, m.rows[2]);
        const float R00=r0[0], R01=r0[1], R02=r0[2];
        const float R10=r1[0], R11=r1[1], R12=r1[2];
        const float R20=r2[0], R21=r2[1], R22=r2[2];

        const float trace = R00 + R11 + R22;
        float x_, y_, z_, w_;
        if (trace > 0.f) {
            const float s = fast_inv_sqrt(trace + 1.f) * 0.5f;
            w_ = 0.25f / s;
            x_ = (R21 - R12) * s;
            y_ = (R02 - R20) * s;
            z_ = (R10 - R01) * s;
        } else if (R00 > R11 && R00 > R22) {
            const float s = fast_inv_sqrt(1.f + R00 - R11 - R22) * 0.5f;
            x_ = 0.25f / s;
            y_ = (R01 + R10) * s;
            z_ = (R02 + R20) * s;
            w_ = (R21 - R12) * s;
        } else if (R11 > R22) {
            const float s = fast_inv_sqrt(1.f + R11 - R00 - R22) * 0.5f;
            y_ = 0.25f / s;
            x_ = (R01 + R10) * s;
            z_ = (R12 + R21) * s;
            w_ = (R02 - R20) * s;
        } else {
            const float s = fast_inv_sqrt(1.f + R22 - R00 - R11) * 0.5f;
            z_ = 0.25f / s;
            x_ = (R02 + R20) * s;
            y_ = (R12 + R21) * s;
            w_ = (R10 - R01) * s;
        }
        return Quat(x_, y_, z_, w_);
    }

    FORCE_INLINE Mat4 toMat4() const {
        const float xx = x*x, yy = y*y, zz = z*z;
        const float xy = x*y, xz = x*z, yz = y*z;
        const float wx = w*x, wy = w*y, wz = w*z;
        Mat4 m;
        m.rows[0] = _mm_set_ps(0.f, 2*(xz+wy),   2*(xy-wz),   1-2*(yy+zz));
        m.rows[1] = _mm_set_ps(0.f, 2*(yz-wx),   1-2*(xx+zz), 2*(xy+wz)  );
        m.rows[2] = _mm_set_ps(0.f, 1-2*(xx+yy), 2*(yz+wx),   2*(xz-wy)  );
        m.rows[3] = _mm_set_ps(1.f, 0.f,          0.f,          0.f        );
        return m;
    }
    FORCE_INLINE Quat operator*(const Quat& b) const {
        return Quat(
            w*b.x + x*b.w + y*b.z - z*b.y,
            w*b.y - x*b.z + y*b.w + z*b.x,
            w*b.z + x*b.y - y*b.x + z*b.w,
            w*b.w - x*b.x - y*b.y - z*b.z
        );
    }

    FORCE_INLINE Quat conjugate()  const { return Quat(-x, -y, -z,  w); }
    FORCE_INLINE Quat operator-()  const { return Quat(-x, -y, -z, -w); }

    FORCE_INLINE float dot(const Quat& b) const {
        __m128 m = _mm_mul_ps(load(), b.load());
        __m128 s = _mm_add_ps(m, _mm_shuffle_ps(m, m, _MM_SHUFFLE(2,3,0,1)));
        s = _mm_add_ps(s, _mm_shuffle_ps(s, s, _MM_SHUFFLE(1,0,3,2)));
        return _mm_cvtss_f32(s);
    }

    FORCE_INLINE Quat normalize() const {
        __m128 ml  = load();
        __m128 m2  = _mm_mul_ps(ml, ml);
        __m128 sum = _mm_add_ps(m2, _mm_shuffle_ps(m2, m2, _MM_SHUFFLE(2,3,0,1)));
        sum = _mm_add_ps(sum, _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(1,0,3,2)));
        float lenSq; _mm_store_ss(&lenSq, sum);
        if (is_zero_lenSq(lenSq)) return identity();
        __m128 r  = _mm_rsqrt_ss(sum);
        __m128 nr = _mm_mul_ss(_mm_mul_ss(sum, r), r);
        r = _mm_mul_ss(r, _mm_sub_ss(_mm_set_ss(1.5f), _mm_mul_ss(_mm_set_ss(0.5f), nr)));
        r = _mm_shuffle_ps(r, r, 0);
        return Quat(_mm_mul_ps(ml, r));
    }

    static FORCE_INLINE Quat nlerp(Quat a, const Quat& b, float t) {
        if (a.dot(b) < 0.f) a = -a;
        const __m128 T  = _mm_set1_ps(t);
        const __m128 T1 = _mm_set1_ps(1.f - t);
        Quat q(_mm_add_ps(_mm_mul_ps(a.load(), T1), _mm_mul_ps(b.load(), T)));
        return q.normalize();
    }

    static Quat slerp(Quat a, const Quat& b, float t) {
        float d = a.dot(b);
        if (d < 0.f) { a = -a; d = -d; }
        if (d > 0.9998f) return nlerp(a, b, t);

        const float theta0 = fast_asin(fast_sqrt(1.f - d*d));
        const float theta  = theta0 * t;
        const float s0 = fast_cos(theta) - d * fast_sin(theta) / fast_sin(theta0);
        const float s1 = fast_sin(theta) / fast_sin(theta0);
        const __m128 S0 = _mm_set1_ps(s0);
        const __m128 S1 = _mm_set1_ps(s1);
        return Quat(_mm_add_ps(_mm_mul_ps(a.load(), S0), _mm_mul_ps(b.load(), S1)));
    }

    FORCE_INLINE Vec3 rotate(const Vec3& v) const {
        const Vec3 u(x, y, z);
        const float s = w;
        return u * (2.f * u.dot(v))
             + v  * (s*s - u.dot(u))
             + u.cross(v) * (2.f * s);
    }
};

#endif // QUAT_H