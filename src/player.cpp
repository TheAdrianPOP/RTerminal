#define NOMINMAX
#include "player.h"
#include "constants.h"
#include "collision_utils.h" // Include for collision helper functions
#include "vector.h" // Include again just to be safe

#include <cmath>
#include <algorithm>
#include <iostream> // For potential debugging output if needed

PlayerController::PlayerController()
    : player_center_pos(0.0, 0.0, 0.0),
      velocity(0.0, 0.0, 0.0),
      camera_yaw(0.0),
      camera_pitch(0.0),
      physics_accumulator(0.0),
      is_on_ground(false),
      is_crouching(false),
      previous_player_pos(0.0, 0.0, 0.0)
{
    // Initial capsule state can be set here or in initialize
    player_capsule.updatePosition(player_center_pos, player_stand_height, player_radius);
}

void PlayerController::initialize(const Vec3& initial_pos, double initial_yaw, double initial_pitch) {
    player_center_pos = initial_pos;
    camera_yaw = initial_yaw;
    camera_pitch = initial_pitch;
    velocity = Vec3(0.0, 0.0, 0.0);
    is_on_ground = false;
    is_crouching = false;
    physics_accumulator = 0.0;
    previous_player_pos = player_center_pos;
    player_capsule.updatePosition(player_center_pos, player_stand_height, player_radius);
}

void PlayerController::update(double deltaTime, const InputManager& input, const Scene& scene) {
    // Update physics accumulator
    physics_accumulator += deltaTime;
    previous_player_pos = player_center_pos;

    // Handle input for movement and crouching
    Vec3 camera_dir = getCameraDirection();
    Vec3 global_up(0.0, 1.0, 0.0);
    Vec3 camera_right = camera_dir.cross(global_up).normalize();
    Vec3 desired_move_direction = input.getMovementDirection(camera_dir, camera_right);

    // Use platform-agnostic KEY_ definitions
    is_crouching = input.isKeyDown(KEY_CONTROL) || input.isKeyDown('C');
    double current_player_height = is_crouching ? player_crouch_height : player_stand_height;

    // Update camera angles based on input
    double yaw_delta, pitch_delta;
    input.getCameraRotationDelta(yaw_delta, pitch_delta, PLAYER_CAMERA_ROTATION_SPEED, deltaTime);
    camera_yaw += yaw_delta;
    camera_pitch += pitch_delta;
    camera_pitch = std::clamp(camera_pitch, -89.0, 89.0); // Clamp pitch

    // Perform physics steps
    while (physics_accumulator >= fixed_physics_timestep) {
        physicsStep(desired_move_direction, scene);
        physics_accumulator -= fixed_physics_timestep;
    }

    // Ensure the capsule position is updated to the final player position for camera and rendering
    player_capsule.updatePosition(player_center_pos, current_player_height, player_radius);

    // Handle jump input after physics steps
    // Use platform-agnostic KEY_ definitions
    if (input.isKeyDown(KEY_SPACE) && is_on_ground) {
        velocity.y = PLAYER_JUMP_FORCE;
        is_on_ground = false;
    }
}

Vec3 PlayerController::getPosition() const {
    return player_center_pos;
}

Vec3 PlayerController::getCameraPosition() const {
    double current_player_height = is_crouching ? player_crouch_height : player_stand_height;
    double current_eye_height_relative = is_crouching ? eye_height_relative_crouch : eye_height_relative_stand;
    // Camera is positioned above the *base* of the capsule
    return player_capsule.base + Vec3(0.0, current_eye_height_relative, 0.0);
}

Vec3 PlayerController::getCameraDirection() const {
    double yaw_rad = camera_yaw * M_PI / 180.0;
    double pitch_rad = camera_pitch * M_PI / 180.0;
    Vec3 dir;
    dir.x = cos(pitch_rad) * sin(yaw_rad);
    dir.y = sin(pitch_rad);
    dir.z = cos(pitch_rad) * -cos(yaw_rad);
    return dir.normalize();
}

Vec3 PlayerController::getCameraUp() const {
    // Calculate camera up based on camera right and direction
    Vec3 global_up(0.0, 1.0, 0.0);
    Vec3 camera_dir = getCameraDirection();
    Vec3 camera_right = camera_dir.cross(global_up).normalize();
    return camera_right.cross(camera_dir).normalize();
}


void PlayerController::physicsStep(const Vec3& desired_move_direction, const Scene& scene) {
    Vec3 pos_before_step = player_center_pos;
    double current_player_height = is_crouching ? player_crouch_height : player_stand_height;

    // Apply Forces & Integrate
    if (!is_on_ground) velocity.y += gravity * fixed_physics_timestep;

    // Apply friction
    double current_friction_base = is_on_ground ? PLAYER_GROUND_FRICTION_EXP_BASE : PLAYER_AIR_FRICTION_EXP_BASE;
    velocity.x *= std::pow(current_friction_base, fixed_physics_timestep * PLAYER_FRICTION_TIMESCALE);
    velocity.z *= std::pow(current_friction_base, fixed_physics_timestep * PLAYER_FRICTION_TIMESCALE);

    // Apply acceleration towards desired movement direction
    double acceleration = is_on_ground ? PLAYER_GROUND_ACCELERATION : PLAYER_AIR_ACCELERATION;
    double max_speed = is_on_ground ? PLAYER_MAX_GROUND_SPEED : PLAYER_MAX_AIR_SPEED;
    Vec3 desired_velocity = desired_move_direction * max_speed;
    Vec3 velocity_change = (desired_velocity - Vec3(velocity.x, 0, velocity.z)); // Only accelerate horizontally

    double max_change_mag = acceleration * fixed_physics_timestep;
    if (velocity_change.magnitudeSq() > max_change_mag * max_change_mag) {
        velocity_change = velocity_change.normalize() * max_change_mag;
    }
    velocity.x += velocity_change.x;
    velocity.z += velocity_change.z;

    // Clamp horizontal speed
    double horiz_speed_sq = velocity.x * velocity.x + velocity.z * velocity.z;
    if (horiz_speed_sq > max_speed * max_speed) {
        double scale = max_speed / std::sqrt(horiz_speed_sq);
        velocity.x *= scale;
        velocity.z *= scale;
    }

    // Proposed next position
    Vec3 target_pos = player_center_pos + velocity * fixed_physics_timestep;

    // Collision Detection & Resolution
    Vec3 step_velocity = velocity; // Velocity to be modified by collisions this step
    bool ground_contact_this_step = false;

    // Update capsule for collision testing at the target position
    player_capsule.updatePosition(target_pos, current_player_height, player_radius);

    // Broad phase collision check (using BVH)
    AABB capsule_aabb;
    capsule_aabb.expand(player_capsule.getSegmentA() - Vec3(player_capsule.radius, player_capsule.radius, player_capsule.radius));
    capsule_aabb.expand(player_capsule.getSegmentB() + Vec3(player_capsule.radius, player_capsule.radius, player_capsule.radius));
    std::vector<const Object*> potential_colliders;
    scene.bvh.queryOverlap(capsule_aabb, potential_colliders);


    // Narrow phase collision check and resolution (iterate multiple times for robustness)
    for (int iter = 0; iter < PLAYER_COLLISION_ITERATIONS; ++iter) {
        bool collision_found = false;

        // Update capsule position for this iteration based on the current target_pos
        player_capsule.updatePosition(target_pos, current_player_height, player_radius);
        const Vec3 seg_a = player_capsule.getSegmentA();
        const Vec3 seg_b = player_capsule.getSegmentB();

        for (const Object* obj_ptr : potential_colliders) {
            Vec3 v0, v1, v2;
            if (!obj_ptr || !obj_ptr->getTriangleVertices(v0, v1, v2)) continue; // Skip invalid objects

            // Collision check between capsule segment (seg_a to seg_b) and triangle (v0, v1, v2)
            // Find the closest points between the triangle and the capsule's center segment
            Vec3 pt_on_tri = closestPointOnTriangle(seg_a, v0, v1, v2); // Approximate using seg_a for simplicity, proper would use closest point on segment
            Vec3 pt_on_seg = closestPointOnLineSegment(pt_on_tri, seg_a, seg_b);

            Vec3 delta = pt_on_seg - pt_on_tri;
            double dist_sq = delta.magnitudeSq();
            double radius_sq = player_capsule.radius * player_capsule.radius;

            if (dist_sq < radius_sq - EPSILON) { // Collision detected
                collision_found = true;
                double dist = (dist_sq > EPSILON*EPSILON) ? std::sqrt(dist_sq) : 0.0;
                double penetration = player_capsule.radius - dist;

                Vec3 normal = (dist > EPSILON) ? delta.normalize() : Vec3(0, 1, 0); // Default to pushing up if overlap is exact
                if (normal.magnitudeSq() < EPSILON*EPSILON) normal = Vec3(0, 1, 0);

                // Push out the target position
                target_pos = target_pos + normal * (penetration + COLLISION_SKIN_WIDTH * 0.1); // Push out slightly more than penetration

                // Adjust velocity based on collision normal
                double vel_dot_normal = step_velocity.dot(normal);
                if (vel_dot_normal < 0) {
                    step_velocity = step_velocity - normal * vel_dot_normal; // Reflect velocity component
                }

                // Check for ground contact
                if (normal.y > GROUND_NORMAL_Y_THRESHOLD) {
                    ground_contact_this_step = true;
                }

                // Prevent sticking to ceilings
                if (normal.y < -GROUND_NORMAL_Y_THRESHOLD && step_velocity.y > 0) {
                    step_velocity.y = 0; // Stop upward velocity if hitting a ceiling
                }
            }
        }
        if (!collision_found) break; // No more collisions in this iteration
    }

    player_center_pos = target_pos;
    velocity = step_velocity;

    is_on_ground = ground_contact_this_step;
    if (is_on_ground) {
        // Prevent sinking into the ground slightly
        velocity.y = std::max(0.0, velocity.y);
    }
}
