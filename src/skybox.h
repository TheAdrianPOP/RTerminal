#ifndef SKYBOX_H
#define SKYBOX_H

#include "vector.h"
#include "ray.h"
#include "scene.h" // Needs Scene definition for getColor
#include "console.h" // Needs ConsoleCell definition for getColor

#include <cmath> // For sin, cos
#include <algorithm> // For clamp
#include <string> // For ASCII_RAMP reference

// Forward declaration of Scene struct to avoid circular includes if Scene includes Skybox
struct Scene;

namespace Skybox {

// Calculates the sun direction based on time (time is assumed to be in a cycle, e.g., 0 to 2*PI)
Vec3 getSunDirection(double current_time);

// Calculates the moon direction based on time (opposite the sun)
Vec3 getMoonDirection(double current_time);

// Calculates the cloud density for a given direction and animation offset [0.0 to 1.0]
double getCloudDensity(const Vec3& direction, const Vec3& cloud_animation_offset, double current_time);

// Gets the color of the skybox for a given ray (now with a dummy parameter to match linker error)
// NOTE: Added dummy_param to match the signature the linker expects.
// This is a diagnostic step to see if it resolves the LNK2001 error.
ConsoleCell getColor(
    const Ray& ray,
    const Scene& scene,
    const Vec3& cloud_animation_offset,
    bool use_background_color_mode,
    double current_time,
    double dummy_param = 0.0 // Added dummy parameter
);

} // namespace Skybox

#endif // SKYBOX_H 