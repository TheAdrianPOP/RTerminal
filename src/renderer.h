#ifndef RENDERER_H
#define RENDERER_H

#include <vector>
#include <string>
#include <memory>
#include <future>
#include <random>
#include <chrono>

#include "console.h"
#include "scene.h"
#include "vector.h"
#include "ray.h"
#include "ui.h" // The Renderer will interact with the UI
#include "console_utils.h" // For UpdateCommand and console functions

// Forward declare Vulkan runtime wrapper to avoid hard dep in header consumers
class VulkanRuntime;

const int TRANSITION_DURATION_FRAMES = 5; // How many frames the effect lasts

class Renderer {
public:
    Renderer(UI& ui_layer_ref); // Constructor takes a reference to the UI layer
    ~Renderer();

    // Main rendering function
    void render(const Scene& scene, int width, int height, const Vec3& camera_pos,
                const Vec3& camera_dir, const Vec3& camera_up, double fov, double fps,
                bool render_ui = true, bool is_loading = false);

    // Raycasting function (can be private if only used internally by render)
    ConsoleCell castRay(const Ray& ray, const Scene& scene, int depth = 0);

    // Methods to manage render state
    void toggleNormalsVisualization();
    bool isNormalsVisualizationEnabled() const { return show_normals_mode; }
    void startTransition(); // Method to initiate the render mode transition
    bool isInTransition() const { return render_mode_transition_frames > 0; }
    void updateTransition(); // Method to decrement transition counter

    // Method to update animation states (called from main loop)
    void updateAnimations(double deltaTime, double time_speed_multiplier, const Vec3& cloud_velocity);

    // Methods to get current state (for UI or external querying)
    bool getShowNormalsMode() const { return show_normals_mode; }
    bool getBackgroundColorMode() const { return use_background_color_mode; }
    int getTransitionFrames() const { return render_mode_transition_frames; }

private:
    // Helper methods extracted from the main render loop
    bool initializeOrResizeBuffers(int width, int height, bool is_loading);
    void applyTransitionEffect(int width, int height);
    std::vector<UpdateCommand> generateUpdateCommands(int width, int height);
    void outputUpdateCommands(const std::vector<UpdateCommand>& update_commands, int height);
    char colorToAsciiChar(const RGBColor& color);

    // Persistent Buffers
    std::vector<std::vector<ConsoleCell>> previous_buffer;
    std::vector<std::vector<ConsoleCell>> current_buffer;

    // Render State Flags
    bool first_frame = true;
    bool use_background_color_mode = false;
    bool show_normals_mode = false;

    // UI reference (Renderer interacts with UI for messages and mode)
    UI& ui_layer;

    // Render mode transition effect state
    int render_mode_transition_frames = 0;

    // Animation states
    Vec3 cloud_animation_offset = Vec3(0.0, 0.0, 0.0);
    double current_time = M_PI; // Start at noon (PI)

    // Random number generators for transition effect (static to persist across calls)
    static std::mt19937 rng;
    static std::uniform_int_distribution<int> char_dist;
    static std::uniform_int_distribution<int> gray_dist;

    // GPU backend (Vulkan) state
    bool use_vulkan_backend = false;
    std::unique_ptr<VulkanRuntime> vulkan;

public:
    // Toggle Vulkan GPU backend
    void toggleVulkanRendering();
    bool isVulkanEnabled() const { return use_vulkan_backend; }
};

#endif // RENDERER_H