#ifndef PLAYER_H
#define PLAYER_H

#include "vector.h"
#include "ray.h" // Might be needed for collision checks if any are ray-based (though using capsule vs triangles now)
#include "aabb.h" // Needed for capsule AABB for BVH query
#include "object.h" // Needed for collision with scene objects
#include "scene.h" // Needed to pass the scene to collision checks
#include "collision_utils.h" // Needed for collision helper functions
#include "input.h" // Needed to get input state
#include "constants.h" // Needed for player constants

#include <algorithm> // For std::max, std::clamp
#include <vector> // For potential_colliders
#include <cmath> // For std::sqrt, std::pow, M_PI

// Capsule primarily for holding player shape
struct Capsule {
    Vec3 base;     // Center of the base sphere
    Vec3 tip;      // Center of the tip sphere
    double radius;
    double height; // Total height (conceptually: distance between sphere centers + 2*radius)

    // Update capsule position based on a center point, total height, and radius
    void updatePosition(const Vec3& center, double current_total_height, double cap_radius) {
        radius = cap_radius;
        height = current_total_height;
        // Calculate half-height of the cylindrical segment connecting the spheres
        double segment_half_height = std::max(0.0, (current_total_height * 0.5) - radius); // Ensure non-negative

        base = center - Vec3(0.0, segment_half_height, 0.0);
        tip = center + Vec3(0.0, segment_half_height, 0.0);
    }

    // Get the line segment defining the capsule's core
    const Vec3& getSegmentA() const { return base; }
    const Vec3& getSegmentB() const { return tip; }
};


// --- Player Controller Class ---
class PlayerController {
public:
    PlayerController();

    // Initialize player state (position and angles)
    void initialize(const Vec3& initial_pos, double initial_yaw, double initial_pitch);

    // Update player state based on input and physics
    void update(double deltaTime, const InputManager& input, const Scene& scene);

    // Get the player's current position (center of the capsule base)
    Vec3 getPosition() const;

    // Get the current camera position (eye height above player base)
    Vec3 getCameraPosition() const;

    // Get camera angles
    double getYaw() const { return camera_yaw; }
    double getPitch() const { return camera_pitch; }
    Vec3 getCameraDirection() const; // Calculate direction vector from angles
    Vec3 getCameraUp() const; // Calculate up vector from direction

private:
    Vec3 player_center_pos; // Center of the player capsule
    Vec3 velocity;
    Capsule player_capsule;

    double camera_yaw; // Yaw angle in degrees
    double camera_pitch; // Pitch angle in degrees

    double physics_accumulator;
    bool is_on_ground;
    bool is_crouching;

    Vec3 previous_player_pos; // For physics step collision

    // Physics constants (can be read from constants.h)
    const double player_stand_height = 0.1;
    const double player_crouch_height = 0.05;
    const double player_radius = 0.25;
    const double eye_height_relative_stand = 0.7;
    const double eye_height_relative_crouch = 0.3;
    const double gravity = -9.81 * PLAYER_GRAVITY_MULTIPLIER;
    const double fixed_physics_timestep = FIXED_PHYSICS_TIMESTEP; // Use the constant

    // Helper function for physics step
    void physicsStep(const Vec3& desired_move_direction, const Scene& scene);
};

#endif // PLAYER_H 