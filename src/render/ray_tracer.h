#pragma once

#include "ray.h"
#include "bvh.h"
#include "triangle.h"
#include "vector.h"
#include "math_utils.h"
#include "console.h"
#include "engine_config.h"

constexpr int   kRTDefaultMaxBounces  = 4;
constexpr float kRTDefaultShadowBias  = 0.001f;
constexpr float kRTDefaultExposure    = 0.0f;
constexpr float kRTDefaultGamma       = 2.2f;
constexpr float kRTDefaultAmbient     = 0.04f;
constexpr float kRTDefaultSunStrength = 2.5f;
constexpr int   kRTAOSamples          = 4;    
constexpr float kRTAORadius           = 0.6f; 
constexpr float kRTAOStrength         = 0.35f;
constexpr float kRTMirrorThreshold    = 0.40f;
constexpr float kRTGlossThreshold     = 0.80f;
constexpr float kRTTransmitThreshold  = 0.001f;
constexpr float kRTMetalThreshold     = 0.5f;

struct RTMaterial {
    Vec3  albedo        = Vec3(0.8f, 0.8f, 0.8f);
    Vec3  emission      = Vec3(0.0f, 0.0f, 0.0f);
    float roughness     = 1.0f;  
    float metalness     = 0.0f;  
    float ior           = 1.5f;  
    float transmission  = 0.0f;  
    float ao_scale      = 1.0f;  
};

struct RTHit {
    Vec3            point;
    Vec3            shading_normal;
    Vec3            face_normal;
    const Triangle* triangle = nullptr;
    float           u = 0.f, v = 0.f;
    float           t = 0.f;
};

class IRTShadingProvider {
public:
    virtual ~IRTShadingProvider() = default;
    virtual void prepareFrame() {}
    virtual RTMaterial getMaterial(const RTHit& hit) const = 0;
    virtual Vec3 getSkyColor(const Ray& ray) const = 0;
};

struct RayTracerConfig {
    int   max_bounces    = kRTDefaultMaxBounces;
    bool  enable_shadows = true;
    bool  enable_ao      = true;
    float shadow_bias    = kRTDefaultShadowBias;
    float exposure       = kRTDefaultExposure;
    float gamma          = kRTDefaultGamma;
    float ambient_light  = kRTDefaultAmbient;

    Vec3  sun_direction  = Vec3(0.55f,  1.0f, 0.35f);
    Vec3  sun_color      = Vec3(1.0f,  0.92f, 0.80f);
    float sun_strength   = kRTDefaultSunStrength;
    float sun_size       = 0.05f;

    float ao_radius      = kRTAORadius;
    float ao_strength    = kRTAOStrength;
    int   ao_samples     = kRTAOSamples;
};

class RayTracer {
public:
    static ConsoleCell trace(const BVH&             bvh,
                              const Ray&             ray,
                              IRTShadingProvider&    provider,
                              const RayTracerConfig& cfg,
                              float                  render_dist,
                              int                    screen_x = 0,
                              int                    screen_y = 0);

private:
    static Vec3 shadePoint(const BVH&             bvh,
                            const Ray&             ray,
                            IRTShadingProvider&    provider,
                            const RayTracerConfig& cfg,
                            float                  render_dist,
                            int                    depth,
                            uint32_t&              rng);

    static bool intersect(const BVH& bvh, const Ray& ray, float render_dist, RTHit& hit);

    static FORCE_INLINE Vec3 reflect(const Vec3& d, const Vec3& n) {
        return d - n * (2.0f * d.dot(n));
    }

    static FORCE_INLINE bool refract(const Vec3& d, const Vec3& n, float eta, Vec3& out_t) {
        float cos_i  = -d.dot(n);
        float sin2_t = eta * eta * (1.0f - cos_i * cos_i);
        if (sin2_t >= 1.0f) return false;
        out_t = d * eta + n * (eta * cos_i - fast_sqrt(1.0f - sin2_t));
        return true;
    }

    static FORCE_INLINE float schlick(float cos_theta, float r0) {
        float t  = 1.0f - cos_theta;
        float t2 = t * t;
        return r0 + (1.0f - r0) * t2 * t2 * t;
    }

    static FORCE_INLINE float r0FromIOR(float ior) {
        float k = (1.0f - ior) / (1.0f + ior);
        return k * k;
    }

    static FORCE_INLINE void buildBasis(const Vec3& n, Vec3& t, Vec3& b) {
        if (fast_abs(n.x) > 0.9f)
            t = Vec3(0.f, 1.f, 0.f).cross(n).normalize();
        else
            t = Vec3(1.f, 0.f, 0.f).cross(n).normalize();
        b = n.cross(t);
    }

    static float computeAO(const BVH& bvh, const RTHit& hit, const RayTracerConfig& cfg, uint32_t rng_seed);

    static FORCE_INLINE uint32_t lcgNext(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return s;
    }
    static FORCE_INLINE float lcgFloat(uint32_t& s) {
        return (float)(lcgNext(s) >> 9) * (1.0f / (float)(1u << 23));
    }

    static FORCE_INLINE Vec3 hemisphereSample(float u1, float u2, const Vec3& n, const Vec3& t, const Vec3& b) {
        float r   = fast_sqrt(u1);
        float phi = TWO_PI * u2;
        float x   = r * fast_cos(phi);
        float y   = r * fast_sin(phi);
        float z   = fast_sqrt(fast_max(0.0f, 1.0f - u1));
        return t * x + b * y + n * z;
    }

    static FORCE_INLINE Vec3 toneMapReinhard(const Vec3& c) {
        return Vec3(c.x / (c.x + 1.f), c.y / (c.y + 1.f), c.z / (c.z + 1.f));
    }

    static FORCE_INLINE int toU8(float v, float inv_gamma) {
        return (int)(fast_pow(fast_clamp(v, 0.f, 1.f), inv_gamma) * 255.f + 0.5f);
    }
};