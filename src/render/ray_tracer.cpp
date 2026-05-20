#include "ray_tracer.h"

ConsoleCell RayTracer::trace(const BVH& bvh, const Ray& ray, IRTShadingProvider& provider,
                              const RayTracerConfig& cfg, float render_dist, int screen_x, int screen_y)
{
    uint32_t rng = (uint32_t)(screen_x * 1973u + screen_y * 9277u + 13u);
    lcgNext(rng);

    Vec3 color = shadePoint(bvh, ray, provider, cfg, render_dist, 0, rng);

    if (cfg.exposure != 0.0f) {
        float scale = fast_exp2(cfg.exposure);
        color = color * scale;
    }
    color = toneMapReinhard(color);

    float inv_gamma = 1.0f / fast_max(cfg.gamma, 0.1f);
    RGBColor rgb(toU8(color.x, inv_gamma), toU8(color.y, inv_gamma), toU8(color.z, inv_gamma));

    ConsoleCell cell;
    cell.character = ConsoleGlyph(' ');
    cell.color     = rgb;
    cell.is_ui     = false;
    cell.is_border = false;
    return cell;
}

bool RayTracer::intersect(const BVH& bvh, const Ray& ray, float render_dist, RTHit& hit) {
    if (bvh.nodes.empty()) return false;
    float t = render_dist;
    const Triangle* tri = nullptr;
    Vec3 n; float u, v;

    if (!bvh.intersect(ray, t, tri, n, u, v) || !tri) return false;

    float b0 = 1.0f - u - v;
    hit.point          = ray.origin + ray.direction * t;
    hit.shading_normal = n;
    hit.face_normal    = tri->getFaceNormal();
    hit.triangle       = tri;
    hit.u              = tri->uv0.u * b0 + tri->uv1.u * u + tri->uv2.u * v;
    hit.v              = tri->uv0.v * b0 + tri->uv1.v * u + tri->uv2.v * v;
    hit.t              = t;
    return true;
}

float RayTracer::computeAO(const BVH& bvh, const RTHit& hit, const RayTracerConfig& cfg, uint32_t rng_seed) {
    const int   num_samples = fast_clamp(cfg.ao_samples, 1, kRTAOSamples);
    const float ao_dist     = cfg.ao_radius;
    Vec3 t, b; buildBasis(hit.shading_normal, t, b);
    int occluded = 0;

    for (int i = 0; i < num_samples; ++i) {
        uint32_t s = rng_seed ^ (uint32_t)(i * 2654435761u);
        Vec3 dir = hemisphereSample(lcgFloat(s), lcgFloat(s), hit.shading_normal, t, b);
        Ray ao_ray(hit.point + hit.shading_normal * cfg.shadow_bias, dir);
        if (bvh.intersectsAny(ao_ray, ao_dist)) ++occluded;
    }
    return (float)occluded / (float)num_samples;
}

Vec3 RayTracer::shadePoint(const BVH& bvh, const Ray& ray, IRTShadingProvider& provider,
                            const RayTracerConfig& cfg, float render_dist, int depth, uint32_t& rng)
{
    if (!bvh.nodes.empty()) {
        const AABB& root = bvh.nodes[0].bounds;
        if (ray.origin.y > root.max.y && ray.direction.y > 0.f) return provider.getSkyColor(ray);
        if (ray.origin.y < root.min.y && ray.direction.y < 0.f) return Vec3(0.f, 0.f, 0.f);
    }

    RTHit hit;
    if (!intersect(bvh, ray, render_dist, hit)) return provider.getSkyColor(ray);

    RTMaterial mat = provider.getMaterial(hit);
    Vec3 radiance = mat.emission;

    bool inside = (ray.direction.dot(hit.shading_normal) > 0.f);
    Vec3 shading_n = inside ? Vec3(-hit.shading_normal.x, -hit.shading_normal.y, -hit.shading_normal.z) : hit.shading_normal;
    Vec3 offset_origin = hit.point + shading_n * cfg.shadow_bias;
    Vec3 sun_dir = Vec3(cfg.sun_direction.x, cfg.sun_direction.y, cfg.sun_direction.z).normalize();

    if (inside && mat.transmission < kRTTransmitThreshold) {
        mat.roughness = 1.0f; 
        mat.metalness = 0.0f;
    }

    if (mat.transmission > kRTTransmitThreshold && depth < cfg.max_bounces) {
        float eta   = inside ? mat.ior : (1.0f / mat.ior);
        float cos_i = fast_clamp(-ray.direction.dot(shading_n), 0.f, 1.f);
        float r0    = r0FromIOR(mat.ior);
        float refl  = schlick(cos_i, r0);

        Vec3 refr_dir;
        bool can_refract = refract(ray.direction, shading_n, eta, refr_dir);
        Vec3 transmitted(0.f, 0.f, 0.f);
        Vec3 reflected(0.f, 0.f, 0.f);

        bool trace_refl = true;
        
        if (can_refract) {
            if (inside) trace_refl = false;
            
            if (!inside && refl < 0.04f) trace_refl = false;

            Ray refr_ray(hit.point - shading_n * cfg.shadow_bias, refr_dir);
            transmitted = shadePoint(bvh, refr_ray, provider, cfg, render_dist, depth + 1, rng);
        } else {
            refl = 1.0f; 
            trace_refl = true;
        }

        if (trace_refl) {
            Vec3 refl_dir = reflect(ray.direction, shading_n);
            Ray refl_ray(offset_origin, refl_dir);
            reflected = shadePoint(bvh, refl_ray, provider, cfg, render_dist, depth + 1, rng);
        }

        Vec3 tint = mat.transmission > 0.5f ? mat.albedo : Vec3(1.f, 1.f, 1.f);
        radiance = radiance + reflected * refl + transmitted * (1.0f - refl) * tint;
        return radiance;
    }

    if (mat.roughness < kRTMirrorThreshold && depth < cfg.max_bounces) {
        Vec3 refl_dir = reflect(ray.direction, shading_n);
        
        if (mat.roughness > 0.001f) {
            Vec3 t, b; buildBasis(refl_dir, t, b);
            float spread = mat.roughness * 0.4f; 
            float r = fast_sqrt(lcgFloat(rng)) * spread;
            float theta = TWO_PI * lcgFloat(rng);
            refl_dir = (refl_dir + t * (r * fast_cos(theta)) + b * (r * fast_sin(theta))).normalize();
            
            if (refl_dir.dot(shading_n) < 0.0f) {
                refl_dir = refl_dir - shading_n * (2.0f * refl_dir.dot(shading_n));
            }
        }

        Ray refl_ray(offset_origin, refl_dir);
        Vec3 refl_color = shadePoint(bvh, refl_ray, provider, cfg, render_dist, depth + 1, rng);

        float cos_r = fast_clamp(-ray.direction.dot(shading_n), 0.f, 1.f);
        float r0    = r0FromIOR(mat.ior);
        float fres  = schlick(cos_r, r0);

        Vec3 F;
        if (mat.metalness >= kRTMetalThreshold) {
            F = Vec3(mat.albedo.x + (1.f - mat.albedo.x) * fres,
                     mat.albedo.y + (1.f - mat.albedo.y) * fres,
                     mat.albedo.z + (1.f - mat.albedo.z) * fres);
        } else {
            F = Vec3(r0 + (1.f - r0) * fres, r0 + (1.f - r0) * fres, r0 + (1.f - r0) * fres);
        }

        radiance = radiance + Vec3(refl_color.x * F.x, refl_color.y * F.y, refl_color.z * F.z);

        if (mat.roughness < 0.1f) return radiance;
    }

    float n_dot_l = fast_clamp(shading_n.dot(sun_dir), 0.f, 1.f);

    if (n_dot_l > 0.001f) {
        bool in_shadow = false;
        
        if (cfg.enable_shadows && depth == 0) {
            Vec3 shadow_dir = sun_dir;
            
            if (cfg.sun_size > 0.001f) {
                Vec3 t, b; buildBasis(sun_dir, t, b);
                float r = fast_sqrt(lcgFloat(rng)) * cfg.sun_size;
                float theta = TWO_PI * lcgFloat(rng);
                shadow_dir = (sun_dir + t * (r * fast_cos(theta)) + b * (r * fast_sin(theta))).normalize();
            }

            Ray shadow_ray(offset_origin, shadow_dir);
            in_shadow = bvh.intersectsAny(shadow_ray, render_dist);
        }

        if (!in_shadow) {
            Vec3 light = cfg.sun_color * (cfg.sun_strength * n_dot_l);
            Vec3 diff  = Vec3(mat.albedo.x * light.x, mat.albedo.y * light.y, mat.albedo.z * light.z);

            if (mat.metalness >= kRTMetalThreshold) diff = diff * (1.0f - mat.metalness);

            if (mat.roughness < kRTGlossThreshold && mat.metalness < kRTMetalThreshold) {
                Vec3  half_v  = (sun_dir - ray.direction).normalize();
                float n_dot_h = fast_clamp(shading_n.dot(half_v), 0.f, 1.f);
                float shininess = 2.0f / fast_max(mat.roughness * mat.roughness, 0.001f);
                float spec_val  = fast_pow(n_dot_h, shininess);
                float fres  = schlick(fast_clamp(half_v.dot(-ray.direction), 0.f, 1.f), r0FromIOR(mat.ior));

                Vec3 specular = Vec3(cfg.sun_color.x * (cfg.sun_strength * spec_val * fres),
                                     cfg.sun_color.y * (cfg.sun_strength * spec_val * fres),
                                     cfg.sun_color.z * (cfg.sun_strength * spec_val * fres));
                diff = diff + specular;
            }
            radiance = radiance + diff;
        }
    }

    float ao = 0.f;
    if (cfg.enable_ao && mat.ao_scale > 0.001f && depth == 0) {
        uint32_t ao_seed = rng ^ (uint32_t)(hit.point.x * 73856093u) ^ (uint32_t)(hit.point.z * 19349663u);
        ao = computeAO(bvh, hit, cfg, ao_seed) * mat.ao_scale * cfg.ao_strength;
    }

    Vec3 sky_up = provider.getSkyColor(Ray(hit.point, shading_n));
    Vec3 ambient = Vec3(sky_up.x * mat.albedo.x, sky_up.y * mat.albedo.y, sky_up.z * mat.albedo.z) 
                   * cfg.ambient_light * (1.0f - ao);

    return radiance + ambient;
}