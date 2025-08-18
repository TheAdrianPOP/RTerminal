#ifndef CONSTANTS_H
#define CONSTANTS_H

#define M_PI 3.14159265358979323846

// --- Console & UI ---
constexpr int MIN_CONSOLE_WIDTH = 80;
constexpr int MIN_CONSOLE_HEIGHT = 25; // Increased minimum height
constexpr int UI_STATUS_BAR_HEIGHT = 3; // Height of the status bar area
const char* const ANSI_RESET = "\x1b[0m";

// --- Rendering & Physics ---
constexpr double TARGET_FPS = 60.0;
constexpr double MAX_DELTA_TIME_SCALE = 5.0; // Limit deltaTime jumps
constexpr double FIXED_PHYSICS_TIMESTEP = 1.0 / 120.0; // Run physics at 120Hz
constexpr double PLAYER_COLLISION_ITERATIONS = 5; // Iterations for collision resolution
constexpr double EPSILON = 1e-6; // Small value for float comparisons
constexpr double COLLISION_SKIN_WIDTH = 0.005; // Small distance to push out after collision
constexpr double GAMMA_CORRECTION = 2.2; // For linearizing colors before display
constexpr double LIGHT_ATTENUATION_LINEAR = 5.0; // Linear light attenuation factor
constexpr double LIGHT_ATTENUATION_QUADRATIC = 1.0; // Quadratic light attenuation factor
constexpr double PROCEDURAL_BUMP_STRENGTH = 1.0; // Small ambient factor


// --- BVH Constants ---
constexpr int MAX_OBJECTS_PER_LEAF = 8; // Example value, adjust as needed
constexpr int MAX_BVH_DEPTH = 32;       // Example value, adjust as needed

// --- Player ---
constexpr double PLAYER_CAMERA_ROTATION_SPEED = 90.0; // Degrees per second
constexpr double PLAYER_GROUND_ACCELERATION = 30.0;
constexpr double PLAYER_AIR_ACCELERATION = 10.0;
constexpr double PLAYER_MAX_GROUND_SPEED = 5.0;
constexpr double PLAYER_MAX_AIR_SPEED = 4.0;
constexpr double PLAYER_GROUND_FRICTION_EXP_BASE = 0.0001; // Lower base = more friction
constexpr double PLAYER_AIR_FRICTION_EXP_BASE = 0.1;
constexpr double PLAYER_FRICTION_TIMESCALE = 1.0; // Multiplier for friction timestep effect
constexpr double PLAYER_JUMP_FORCE = 4; // Reduced jump force for a more realistic feel
constexpr double PLAYER_GRAVITY_MULTIPLIER = 1.0; // Increased gravity multiplier
constexpr double GROUND_NORMAL_Y_THRESHOLD = 0.7; // Minimum Y component for ground normal
constexpr double DEFAULT_PLAYER_YAW = 0.0;
constexpr double DEFAULT_PLAYER_PITCH = 0.0;
constexpr double DEFAULT_FOV = 70.0;

// --- Skybox Constants ---
// Day Sky Colors
constexpr double SKYBOX_DAY_ZENITH_COLOR_R = 0.3;
constexpr double SKYBOX_DAY_ZENITH_COLOR_G = 0.5;
constexpr double SKYBOX_DAY_ZENITH_COLOR_B = 0.9;

constexpr double SKYBOX_DAY_HORIZON_COLOR_R = 0.7;
constexpr double SKYBOX_DAY_HORIZON_COLOR_G = 0.85;
constexpr double SKYBOX_DAY_HORIZON_COLOR_B = 1.0;

// Night Sky Colors (Darker gradient)
constexpr double SKYBOX_NIGHT_ZENITH_COLOR_R = 0.05;
constexpr double SKYBOX_NIGHT_ZENITH_COLOR_G = 0.05;
constexpr double SKYBOX_NIGHT_ZENITH_COLOR_B = 0.15;

constexpr double SKYBOX_NIGHT_HORIZON_COLOR_R = 0.1;
constexpr double SKYBOX_NIGHT_HORIZON_COLOR_G = 0.1;
constexpr double SKYBOX_NIGHT_HORIZON_COLOR_B = 0.25;

constexpr double SKYBOX_BELOW_HORIZON_COLOR_R = 0.5;
constexpr double SKYBOX_BELOW_HORIZON_COLOR_G = 0.5;
constexpr double SKYBOX_BELOW_HORIZON_COLOR_B = 0.6;

// Sun
constexpr double SKYBOX_SUN_COLOR_R = 1.0;
constexpr double SKYBOX_SUN_COLOR_G = 0.9;
constexpr double SKYBOX_SUN_COLOR_B = 0.8;
constexpr double SKYBOX_SUN_SIZE = 0.03; // Angular size approximation

// Moon
constexpr double SKYBOX_MOON_COLOR_R = 0.7;
constexpr double SKYBOX_MOON_COLOR_G = 0.7;
constexpr double SKYBOX_MOON_COLOR_B = 0.75;
constexpr double SKYBOX_MOON_SIZE = 0.025; // Angular size approximation

// Clouds
constexpr int SKYBOX_CLOUD_NUM_OCTAVES = 6;
constexpr double SKYBOX_CLOUD_INITIAL_SCALE = 2.5;
constexpr double SKYBOX_CLOUD_LACUNARITY = 2.2;
constexpr double SKYBOX_CLOUD_PERSISTENCE = 0.45;
constexpr double SKYBOX_CLOUD_COVER = 0.7;
constexpr double SKYBOX_CLOUD_SHARPNESS = 0.3;
constexpr double SKYBOX_CLOUD_COLOR_R = 0.95;
constexpr double SKYBOX_CLOUD_COLOR_G = 0.96;
constexpr double SKYBOX_CLOUD_COLOR_B = 0.98;
constexpr double SKYBOX_CLOUD_ANIMATION_VELOCITY_X = 0.01;
constexpr double SKYBOX_CLOUD_ANIMATION_VELOCITY_Y = 0.0;
constexpr double SKYBOX_CLOUD_ANIMATION_VELOCITY_Z = 0.005; // Units per second

// --- Time of Day ---
constexpr double TIME_SPEED_MULTIPLIER = 0.1; // Controls how fast time passes (e.g., 1.0 = 1 second of real time is 1 unit of 'time')
constexpr double DAY_NIGHT_TRANSITION_ANGLE = 0.1; // Sun Y direction threshold for blending (e.g., 0.1 means blend when sun is slightly above horizon)


// --- ASCII Ramp ---
// Using a wider range of characters for better fidelity
#include <string> // Needed for std::string
const std::string ASCII_RAMP = "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. ";


#endif // CONSTANTS_H
