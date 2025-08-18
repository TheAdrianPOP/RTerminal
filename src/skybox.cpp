#include "skybox.h"
#include "constants.h"
#include "noise.h"
#include "ray.h"
#include "scene.h" // Needed for scene.fog access
#include "console.h"
#include "shading.h" // For vec3ToColor
#include "vector.h"  // Need to include vector.h here now

#include <cmath>
#include <algorithm>
#include <string>

namespace Skybox {

// Calculates the sun direction based on time (time is assumed to be in a cycle, e.g., 0 to 2*PI)
Vec3 getSunDirection(double current_time) {
    // Simple path in the XZ plane, with Y controlling elevation.
    // Time 0/2PI is midnight (lowest Y), PI is noon (highest Y).
    // X varies from -1 to 1 (e.g., West to East), Z varies from 0 (at X=0) to -1/+1 (at X=+/-1)
    // Let's aim for sunrise around current_time = PI*0.5, noon at PI, sunset around PI*1.5.

    // Angle around the "celestial equator" based on time
    double angle = current_time; // Angle from 0 to 2*PI

    // Map angle to a direction vector
    // X = cos(angle)  (Ranges from 1 to -1 over 2PI, good for East to West)
    // Z = sin(angle)  (Ranges from 0 to 0 over 2PI, needs adjustment for North/South)
    // Y = ??? (Needs to be high at noon, low at midnight)

    // Let's use a simplified mapping:
    // Time 0 (midnight): Sun is down, e.g., (0, -1, 0)
    // Time PI/2 (sunrise): Sun is rising in the East, e.g., (1, 0.1, 0) - needs normalization
    // Time PI (noon): Sun is overhead, e.g., (0, 1, 0)
    // Time 3PI/2 (sunset): Sun is setting in the West, e.g., (-1, 0.1, 0) - needs normalization

    // Simple sinusoidal interpolation based on time mapped to 0-1 for the day arc (0 to PI)
    double day_arc_time = std::fmod(current_time, 2.0 * M_PI);

    // If time is between PI and 2*PI, it's night, sun is below horizon
    if (day_arc_time > M_PI) {
        // Sun is below the horizon during the night phase (PI to 2*PI)
        // Let's just set a consistent below-horizon direction for simplicity
        // Or continue the sinusoidal path below the horizon
         day_arc_time = day_arc_time - 2.0 * M_PI; // Map 0 to -2PI for continuous sin/cos below horizon
    }


    // Sinusoidal path in YZ plane (or XY depending on perspective)
    // Let angle_for_path go from -PI to PI over 2PI time
    double angle_for_path = current_time - M_PI; // Angle from -PI to PI

    double sun_dir_y = std::sin(angle_for_path); // Elevation
    double sun_dir_x = std::cos(angle_for_path); // Horizontal position (East-West?)
    double sun_dir_z = 0.0; // Fixed depth for this simple path

    // Normalize and return the direction
    Vec3 sun_dir(sun_dir_x, sun_dir_y, sun_dir_z);
    return sun_dir.normalize();
}

// Calculates the moon direction based on time (opposite the sun)
Vec3 getMoonDirection(double current_time) {
    // The moon is roughly opposite the sun in the sky.
    // Add PI to the time angle used for the sun path.
    double angle_for_path = current_time - M_PI + M_PI; // Shift phase by PI

    double moon_dir_y = std::sin(angle_for_path); // Elevation
    double moon_dir_x = std::cos(angle_for_path); // Horizontal position
    double moon_dir_z = 0.0; // Keep on the same plane

    Vec3 moon_dir(moon_dir_x, moon_dir_y, moon_dir_z);
    return moon_dir.normalize();
}

// Calculates the cloud density for a given direction and animation offset [0.0 to 1.0]
double getCloudDensity(const Vec3& direction, const Vec3& cloud_animation_offset, double current_time) {
    double total_noise = 0.0;
    double amplitude = 1.0;
    double frequency = SKYBOX_CLOUD_INITIAL_SCALE;
    double max_amplitude = 0.0;

    // Add a slight time component to the noise coordinates for subtle cloud shape evolution
    Vec3 noise_coord_base = direction + cloud_animation_offset + Vec3(current_time * 0.01, 0.0, current_time * 0.005); // Add slight time evolution

    for (int i = 0; i < SKYBOX_CLOUD_NUM_OCTAVES; ++i) {
        Vec3 current_noise_coord = noise_coord_base * frequency;
        total_noise += Perlin::getPerlinNoise().noise(current_noise_coord) * amplitude;

        max_amplitude += amplitude;
        amplitude *= SKYBOX_CLOUD_PERSISTENCE;
        frequency *= SKYBOX_CLOUD_LACUNARITY;
    }

    double multi_octave_noise = (max_amplitude > 0) ? total_noise / max_amplitude : 0.0;
    double cloud_density = std::clamp((multi_octave_noise + 1.0) * 0.5, 0.0, 1.0);

    // Apply shaping function
    double adjusted_density = std::clamp((cloud_density - (1.0 - SKYBOX_CLOUD_COVER)) / SKYBOX_CLOUD_SHARPNESS, 0.0, 1.0);

    return adjusted_density;
}

ConsoleCell getColor(const Ray& ray, const Scene& scene, const Vec3& cloud_animation_offset, bool use_background_color_mode, double current_time, double dummy_param) {
    ConsoleCell sky_cell;

    // Get Sun and Moon directions based on time
    Vec3 sun_dir_sky = getSunDirection(current_time); // This is direction *from* sun
    Vec3 moon_dir_sky = getMoonDirection(current_time);

    // --- Day/Night Sky Color Blending ---
    const Vec3 day_zenith_color(SKYBOX_DAY_ZENITH_COLOR_R, SKYBOX_DAY_ZENITH_COLOR_G, SKYBOX_DAY_ZENITH_COLOR_B);
    const Vec3 day_horizon_color(SKYBOX_DAY_HORIZON_COLOR_R, SKYBOX_DAY_HORIZON_COLOR_G, SKYBOX_DAY_HORIZON_COLOR_B);
    const Vec3 night_zenith_color(SKYBOX_NIGHT_ZENITH_COLOR_R, SKYBOX_NIGHT_ZENITH_COLOR_G, SKYBOX_NIGHT_ZENITH_COLOR_B);
    const Vec3 night_horizon_color(SKYBOX_NIGHT_HORIZON_COLOR_R, SKYBOX_NIGHT_HORIZON_COLOR_G, SKYBOX_NIGHT_HORIZON_COLOR_B);
    const Vec3 below_horizon_color(SKYBOX_BELOW_HORIZON_COLOR_R, SKYBOX_BELOW_HORIZON_COLOR_G, SKYBOX_BELOW_HORIZON_COLOR_B);


    // Determine blending factor based on sun elevation
    // Use smoothstep or similar to make transition smoother
    double sun_elevation_blend_factor = std::clamp(
        (sun_dir_sky.y - (-DAY_NIGHT_TRANSITION_ANGLE)) / (DAY_NIGHT_TRANSITION_ANGLE - (-DAY_NIGHT_TRANSITION_ANGLE)),
        0.0, 1.0);
     sun_elevation_blend_factor = sun_elevation_blend_factor * sun_elevation_blend_factor * (3.0 - 2.0 * sun_elevation_blend_factor); // Smoothstep


    // Interpolate zenith and horizon colors based on time of day
    Vec3 current_zenith_color = day_zenith_color * sun_elevation_blend_factor + night_zenith_color * (1.0 - sun_elevation_blend_factor);
    Vec3 current_horizon_color = day_horizon_color * sun_elevation_blend_factor + night_horizon_color * (1.0 - sun_elevation_blend_factor);


    // Base sky color calculation (gradient) using interpolated colors
    double sky_factor = std::pow(std::clamp(ray.direction.y, 0.0, 1.0), 0.75);
    Vec3 sky_color_vec3 = current_horizon_color * (1.0 - sky_factor) + current_zenith_color * sky_factor;

    // Adjust color if looking below horizon (optional ground haze effect)
    if (ray.direction.y < 0) {
        double below_horizon_factor = std::clamp(-ray.direction.y * 2.0, 0.0, 0.3);
        sky_color_vec3 = sky_color_vec3 * (1.0 - below_horizon_factor) + below_horizon_color * below_horizon_factor;
    }

    // --- Add Sun ---
    const Vec3 sun_color(SKYBOX_SUN_COLOR_R, SKYBOX_SUN_COLOR_G, SKYBOX_SUN_COLOR_B);
    double sun_dot = ray.direction.dot(sun_dir_sky);
    if (sun_dot > 1.0 - SKYBOX_SUN_SIZE) {
        double sun_intensity = std::pow(std::clamp((sun_dot - (1.0 - SKYBOX_SUN_SIZE)) / SKYBOX_SUN_SIZE, 0.0, 1.0), 2.0); // Falloff
        Vec3 direct_sun_color = sun_color * sun_intensity;
        sky_color_vec3 = sky_color_vec3 + direct_sun_color * sun_elevation_blend_factor; // Only add sun during the day blend
    }

    // --- Add Moon ---
    const Vec3 moon_color(SKYBOX_MOON_COLOR_R, SKYBOX_MOON_COLOR_G, SKYBOX_MOON_COLOR_B);
    double moon_dot = ray.direction.dot(moon_dir_sky);
    if (moon_dot > 1.0 - SKYBOX_MOON_SIZE) {
        double moon_intensity = std::pow(std::clamp((moon_dot - (1.0 - SKYBOX_MOON_SIZE)) / SKYBOX_MOON_SIZE, 0.0, 1.0), 2.0); // Falloff
        Vec3 direct_moon_color = moon_color * moon_intensity;
        sky_color_vec3 = sky_color_vec3 + direct_moon_color * (1.0 - sun_elevation_blend_factor); // Only add moon during the night blend
    }


    // --- Add Perlin Noise Clouds (Using the new function and passing time) ---
    double cloud_alpha = getCloudDensity(ray.direction, cloud_animation_offset, current_time); // Pass current_time
    const Vec3 cloud_color(SKYBOX_CLOUD_COLOR_R, SKYBOX_CLOUD_COLOR_G, SKYBOX_CLOUD_COLOR_B);

    // Reduce cloud visibility/color at night
    Vec3 current_cloud_color = cloud_color;
    double cloud_night_fade = (1.0 - sun_elevation_blend_factor) * 0.5; // Fade clouds by up to 50% at night
     current_cloud_color = current_cloud_color * (1.0 - cloud_night_fade);


    sky_color_vec3 = sky_color_vec3 * (1.0 - cloud_alpha) + current_cloud_color * cloud_alpha; // Blend with constructed cloud_color

    // Clamp final sky color
    sky_color_vec3.x = std::clamp(sky_color_vec3.x, 0.0, 1.0);
    sky_color_vec3.y = std::clamp(sky_color_vec3.y, 0.0, 1.0);
    sky_color_vec3.z = std::clamp(sky_color_vec3.z, 0.0, 1.0);

    // Convert final sky Vec3 color to numeric RGBColor
    sky_cell.color = vec3ToColor(sky_color_vec3);

    // Determine sky character based on render mode and brightness
    if (use_background_color_mode) {
        sky_cell.character = ' '; // Background mode uses spaces
    } else {
        // Calculate luminance for ASCII character selection
        double luminance = 0.2126 * sky_color_vec3.x + 0.7152 * sky_color_vec3.y + 0.0722 * sky_color_vec3.z;
        size_t ramp_size = ASCII_RAMP.length();
        luminance = std::clamp(luminance, 0.0, 1.0);
        size_t char_index = static_cast<size_t>(luminance * (ramp_size - 1));
        sky_cell.character = ASCII_RAMP[char_index];
    }

    return sky_cell;
}

} // namespace Skybox 