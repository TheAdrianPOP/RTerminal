#include "shading.h"
#include "constants.h"
#include "vector.h"
#include "material.h"
#include "scene.h"
#include "texture.h"
#include "console.h"
#include "light.h"
#include "ray.h"
#include "bvh.h"

#include <cmath>
#include <algorithm>
#include <string>

// Convert a Vec3 color (RGB, each component in [0,1]) to an RGBColor struct (0-255)
// Renamed from vec3ToAnsi256
RGBColor vec3ToColor(const Vec3& color_in) {
    // Clamp color components to [0,1]
    double r = std::clamp(color_in.x, 0.0, 1.0);
    double g = std::clamp(color_in.y, 0.0, 1.0);
    double b = std::clamp(color_in.z, 0.0, 1.0);

    RGBColor result_color;
    // Convert to 0-255 range for RGB components
    result_color.r = static_cast<int>(r * 255.999); // Add 0.999 for better rounding
    result_color.g = static_cast<int>(g * 255.999);
    result_color.b = static_cast<int>(b * 255.999);

    // Ensure values are clamped just in case
    result_color.r = std::clamp(result_color.r, 0, 255);
    result_color.g = std::clamp(result_color.g, 0, 255);
    result_color.b = std::clamp(result_color.b, 0, 255);

    return result_color;
}

// Calculates the final color and character for a point on a surface based on texture and shadow
ConsoleCell shade(
    const Vec3& point,
    const Vec3& geometric_normal,
    const Material& material,
    const Scene& scene,
    double u,
    double v,
    bool visualize_normals
)
{
    ConsoleCell result_cell;
    Vec3 base_color_from_texture_for_lighting;
    Vec3 base_color_for_bump_calculation;

    bool texture_provided_char = false;
    bool texture_provided_color_info_for_bump = false;

    // 1. Check for Texture Data & Get Base Color
    if (material.texture) {
        std::string dummy_ansi_str; 
        texture_provided_color_info_for_bump = material.texture->getCharacterAndColor(
            u, v,
            result_cell.character,
            dummy_ansi_str, 
            base_color_for_bump_calculation
        );
        if (texture_provided_color_info_for_bump) {
            texture_provided_char = (result_cell.character != ' ' && result_cell.character != '\0');
            base_color_from_texture_for_lighting = base_color_for_bump_calculation;
        }
    }
    if (!texture_provided_color_info_for_bump) {
        base_color_for_bump_calculation = material.color;
        base_color_from_texture_for_lighting = material.color;
    }

    // --- Procedural Bump Mapping (Calculate shading_normal) ---
    Vec3 shading_normal = geometric_normal.normalize();
    if (material.texture && material.texture->getWidth() > 0 && material.texture->getHeight() > 0) {
        double L_center = 0.2126 * base_color_for_bump_calculation.x + 0.7152 * base_color_for_bump_calculation.y + 0.0722 * base_color_for_bump_calculation.z;
        double du = 1.0 / static_cast<double>(material.texture->getWidth());
        double dv = 1.0 / static_cast<double>(material.texture->getHeight());
        Vec3 color_u_plus, color_v_plus;
        char dummy_char_bump; std::string dummy_ansi_bump;
        material.texture->getCharacterAndColor(u + du, v, dummy_char_bump, dummy_ansi_bump, color_u_plus);
        double L_u_plus = 0.2126 * color_u_plus.x + 0.7152 * color_u_plus.y + 0.0722 * color_u_plus.z;
        material.texture->getCharacterAndColor(u, v + dv, dummy_char_bump, dummy_ansi_bump, color_v_plus);
        double L_v_plus = 0.2126 * color_v_plus.x + 0.7152 * color_v_plus.y + 0.0722 * color_v_plus.z;
        double gradient_u = (L_u_plus - L_center) * PROCEDURAL_BUMP_STRENGTH;
        double gradient_v = (L_v_plus - L_center) * PROCEDURAL_BUMP_STRENGTH;
        Vec3 N_geom = geometric_normal.normalize();
        Vec3 T, B;
        if (std::abs(N_geom.x) > std::abs(N_geom.y)) { T = Vec3(-N_geom.z, 0, N_geom.x).normalize(); }
        else { T = Vec3(0, N_geom.z, -N_geom.y).normalize(); }
        if (T.magnitudeSq() < EPSILON * EPSILON) {
            Vec3 arbitrary_vec = (std::abs(N_geom.y) < 0.99) ? Vec3(0,1,0) : Vec3(1,0,0);
            T = N_geom.cross(arbitrary_vec).normalize();
            if (T.magnitudeSq() < EPSILON * EPSILON) { arbitrary_vec = Vec3(0,0,1); T = N_geom.cross(arbitrary_vec).normalize(); }
        }
        B = N_geom.cross(T).normalize();
        shading_normal = (N_geom - T * gradient_u - B * gradient_v).normalize();
    }
    // --- End Procedural Bump Mapping ---

    // --- Shadow Calculation (using shading_normal) ---
    double shadow_multiplier = 0.4;
    if (!scene.lights.empty()) {
        bool lit_by_at_least_one_light = false;
        for (const auto& light_source : scene.lights) { // Iterate through all lights for shadows
            Vec3 light_vector = light_source.position - point;
            double dist_to_light_sq = light_vector.magnitudeSq();
            if (dist_to_light_sq > EPSILON * EPSILON) {
                double dist_to_light = std::sqrt(dist_to_light_sq);
                Vec3 dir_to_light = light_vector / dist_to_light;
                Vec3 shadow_ray_origin = point + shading_normal * (COLLISION_SKIN_WIDTH * 3.0);
                Ray shadow_ray(shadow_ray_origin, dir_to_light);
                double max_shadow_dist = dist_to_light - (COLLISION_SKIN_WIDTH * 3.0);
                if (max_shadow_dist < EPSILON) max_shadow_dist = EPSILON;
                if (!scene.bvh.intersectsAny(shadow_ray, max_shadow_dist)) {
                    lit_by_at_least_one_light = true;
                    break;
                }
            } else {
                lit_by_at_least_one_light = true;
                break;
            }
        }
        if (lit_by_at_least_one_light) {
            shadow_multiplier = 1.0;
        }
    } else {
        shadow_multiplier = 1.0;
    }
    // --- End Shadow Calculation ---

    // --- Basic Diffuse Lighting using shading_normal ---
    double diffuse_intensity = 0.5; // Base ambient/diffuse factor if no lights or NdotL is zero
    if (!scene.lights.empty()) {
        // For simplicity, use the first light as the primary directional source for NdotL
        // This light's 'direction' member is used if it's a directional light,
        // or its 'position' could be used to calculate a direction if it's a point light.
        // Assuming scene.lights[0] is our "sun" and we use its 'direction' if it's meant to be directional.
        // If all your lights are point lights (from scene.txt), this needs adjustment.
        
        // Let's use the direction TO the first light source (point light assumption)
        Vec3 dir_to_first_light = (scene.lights[0].position - point).normalize();
        double NdotL = std::max(0.0, shading_normal.dot(dir_to_first_light));
        
        // Combine a base ambient with the NdotL diffuse
        // Example: 0.3 ambient + 0.7 diffuse effect
        diffuse_intensity = 0.3 + 0.7 * NdotL;
    }
    // --- End Basic Diffuse Lighting ---


    Vec3 final_color_output_vec3;

    if (visualize_normals) {
        Vec3 normal_as_color;
        normal_as_color.x = (shading_normal.x + 1.0) * 0.5;
        normal_as_color.y = (shading_normal.y + 1.0) * 0.5;
        normal_as_color.z = (shading_normal.z + 1.0) * 0.5;
        
        // Apply shadow AND the basic diffuse intensity to the normal color
        final_color_output_vec3 = normal_as_color * diffuse_intensity * shadow_multiplier;

        result_cell.character = '.';
    } else {
        // Determine Final Character (if not already set by texture)
        if (!texture_provided_char) {
            result_cell.character = material.symbol;
            if (result_cell.character == ' ' || result_cell.character == '\0') {
                 double luminance = 0.2126 * base_color_from_texture_for_lighting.x + 0.7152 * base_color_from_texture_for_lighting.y + 0.0722 * base_color_from_texture_for_lighting.z;
                 const std::string& ascii_ramp = ASCII_RAMP;
                 luminance = std::clamp(luminance, 0.0, 1.0);
                 result_cell.character = ascii_ramp[static_cast<size_t>(luminance * (ascii_ramp.length() - 1))];
            }
        }
        
        // Apply diffuse intensity and shadow to the base texture/material color
        final_color_output_vec3 = base_color_from_texture_for_lighting * diffuse_intensity * shadow_multiplier;
    }

    // Clamp final color
    final_color_output_vec3.x = std::clamp(final_color_output_vec3.x, 0.0, 1.0);
    final_color_output_vec3.y = std::clamp(final_color_output_vec3.y, 0.0, 1.0);
    final_color_output_vec3.z = std::clamp(final_color_output_vec3.z, 0.0, 1.0);

    result_cell.color = vec3ToColor(final_color_output_vec3);
    return result_cell;
} 