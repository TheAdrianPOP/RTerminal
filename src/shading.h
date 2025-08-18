#ifndef SHADING_H
#define SHADING_H

#include <string>
#include "vector.h" // For Vec3
#include "material.h"
#include "scene.h" // For Scene reference
#include "console.h" // For ConsoleCell and RGBColor
#include "skybox.h" // Include skybox.h to access getCloudDensity

// Forward declarations to avoid circular includes
class Material;
class Scene;
struct ConsoleCell;
class Light;
class Ray;

// Function to convert Vec3 (0-1 range) to RGBColor (0-255 or reset)
// Renamed from vec3ToAnsi256 to reflect its new purpose
RGBColor vec3ToColor(const Vec3& color_in);

// Main shading function: computes the visual appearance of a point on a surface
ConsoleCell shade(
    const Vec3& point,
    const Vec3& geometric_normal,
    const Material& material,
    const Scene& scene,
    double u,
    double v,
    bool visualize_normals
);

// Helper function to check if a point is in shadow from a specific light
bool isInShadow(
    const Scene& scene,      // Scene containing the BVH for intersection tests
    const Vec3& point,       // World-space point to check
    const Vec3& normal,      // Surface normal at the point 
    const Vec3& light_pos,   // Position of the light source
    double max_distance      // Maximum distance to check (typically distance to light)
);

#endif // SHADING_H 