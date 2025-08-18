#define NOMINMAX
#include "renderer.h"
#include "constants.h"
#include "shading.h"
#include "skybox.h"
#include "console_utils.h" // Include for console functions and UpdateCommand struct
#include "ui.h" // Explicitly include UI header
#include "console.h" // Explicitly include ConsoleCell/RGBColor header
#include "aabb.h" // Required for scene.bvh.intersect
#include "object.h" // Required for const Object*
#include "material.h" // Required for material access
#include "ray.h" // Required for Ray
#include "vector.h" // Required for Vec3
#include "bvh.h" // Required for scene.bvh
#include "triangle.h" // Required for getTriangleVertices (if not fully abstracted by Object)
#include "collision_utils.h" // Required for closestPointOnTriangle/LineSegment

#include <iostream>
#include <string>
#include <sstream>
#include <cmath> // For std::tan, std::sqrt
#include <algorithm> // For std::min, std::max
#include <limits> // For std::numeric_limits
#include <thread> // For std::thread::hardware_concurrency
#include <future> // For std::async, std::future
#include <chrono> // For std::chrono

// Minimal forward include for VulkanRuntime implementation entry
#include "vulkan_runtime.h"

// Initialize static random number generators
std::mt19937 Renderer::rng(std::chrono::steady_clock::now().time_since_epoch().count());
std::uniform_int_distribution<int> Renderer::char_dist(33, 126); // Printable ASCII chars
std::uniform_int_distribution<int> Renderer::gray_dist(50, 200); // Range for grayscale values

// Define constants for animation wrap-around and ANSI output initialization
const double CLOUD_OFFSET_WRAP_AROUND = 2000.0;
const int ANSI_CURSOR_MOVE_BASE = 1; // ANSI cursor position is 1-indexed
const int ANSI_COLOR_DEFAULT = -1; // Sentinel for default/unknown color state
const RGBColor UI_BACKGROUND_COLOR = {30, 30, 40}; // Default background for UI elements
const RGBColor DEFAULT_BACKGROUND_COLOR = {0, 0, 0}; // Default background for non-UI elements in ASCII mode
const int LAST_OUTPUT_X_INIT = -2; // Initial X position for output tracking
const int LAST_OUTPUT_Y_INIT = -1; // Initial Y position for output tracking

// Constructor
Renderer::Renderer(UI& ui_layer_ref) : ui_layer(ui_layer_ref) {
    // Constructor body can be used for any initial setup if needed
}

Renderer::~Renderer() {
    // Ensure Vulkan is shutdown before destruction order issues
    if (vulkan) {
        vulkan->shutdown();
        vulkan.reset();
    }
}

// --- Raycasting (Uses BVH) --- Returns ConsoleCell with numeric color
ConsoleCell Renderer::castRay(const Ray& ray, const Scene& scene, int depth /*, bool normals_mode_active - passed via scene or global for simplicity here, or add to signature*/) {
    // In future, dispatch ray batch to Vulkan when enabled; for now CPU path
    double min_t = std::numeric_limits<double>::max();
    const Object* hit_object = nullptr;
    Vec3 hit_normal;
    double hit_u = 0.0, hit_v = 0.0;
    bool hit = scene.bvh.intersect(ray, min_t, hit_object, hit_normal, hit_u, hit_v);

    if (hit) {
        // --- Hit Scene Geometry ---
        if (!hit_object) {
             return ConsoleCell();
        }
        Vec3 hit_point = ray.origin + ray.direction * min_t;
        // Pass the global show_normals_mode flag to shade
        // Now using the member variable show_normals_mode
        return shade(hit_point, hit_normal, hit_object->material, scene, hit_u, hit_v, show_normals_mode);

    } else {
        // Now using the member variable show_normals_mode
        if (show_normals_mode) { // If visualizing normals, sky should be black or some default
            ConsoleCell normal_sky_cell;
            normal_sky_cell.character = ' ';
            normal_sky_cell.color = RGBColor{0,0,0};
            return normal_sky_cell;
        }
        // --- Skybox Logic (Ray Missed Scene Geometry) ---
        // Now using member variables cloud_animation_offset and current_time
        return Skybox::getColor(ray, scene, cloud_animation_offset, use_background_color_mode, current_time, 0.0);
    }
}

// --- Helper function to initialize or resize persistent buffers ---
bool Renderer::initializeOrResizeBuffers(int width, int height, bool is_loading) {
    bool needs_clear = false;
    // Check if buffers need initialization or resizing
    if (first_frame || static_cast<int>(current_buffer.size()) != height || (height > 0 && static_cast<int>(current_buffer[0].size()) != width)) {
        try {
            if (height <= 0 || width <= 0) {
                 ui_layer.addMessage("Warning: Render buffer invalid dimensions (" + std::to_string(width) + "x" + std::to_string(height) + ")", RGBColor{255, 150, 50});
                 return false; // Indicate failure or no clear needed
            }
            // Assign new buffers of the correct size (ConsoleCell default constructor sets reset color)
            previous_buffer.assign(height, std::vector<ConsoleCell>(width));
            current_buffer.assign(height, std::vector<ConsoleCell>(width));
            needs_clear = true; // Signal that console should be cleared

        } catch (const std::bad_alloc& e) {
            ui_layer.addMessage("ERROR: Failed to allocate console buffers (" + std::to_string(width) + "x" + std::to_string(height) + ")", RGBColor{255, 50, 50});
            return false; // Indicate failure
        }
    } else {
        // Only copy previous buffer if not in loading mode.
        // In loading mode, we might want a clean slate for the UI render.
        if (!is_loading) {
        previous_buffer = current_buffer; // Copy assignment
        } else {
             // If loading, clear the current buffer to the default background
             // or a specific loading background color before rendering UI
             for(auto& row : current_buffer) {
                 for(auto& cell : row) {
                     cell = ConsoleCell(); // Reset to default
                 }
             }
        }
    }
    return needs_clear; // Return whether console needs clearing
}

// --- Helper function to apply loading/transition static effect ---
void Renderer::applyTransitionEffect(int width, int height) {
        // Fill the buffer with a transition pattern/static
        // Use a simple random character and grayscale color effect
        // Using static member rng and distributions
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                 if (y >= 0 && y < current_buffer.size() && x >= 0 && x < current_buffer[y].size()) {
                     uint8_t gray_value = (uint8_t)gray_dist(rng);
                     current_buffer[y][x].character = static_cast<char>(char_dist(rng));
                     // Set grayscale color (R=G=B)
                     current_buffer[y][x].color = RGBColor{gray_value, gray_value, gray_value};
                     current_buffer[y][x].is_ui = false; // Ensure it doesn't interfere with UI rendering later
                 }
            }
        }
}

// --- Helper function to convert RGB color to ASCII character based on luminance ---
char Renderer::colorToAsciiChar(const RGBColor& color) {
    // ASCII characters from dark to light (or sparse to dense)
    // This string can be adjusted for different aesthetics. Space is for the darkest.
    // Using a more detailed character set for finer gradients
    const char ascii_chars[] = "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'."; // More detailed set (69 characters + space)
    int num_chars = sizeof(ascii_chars) - 1; // Exclude null terminator

    // Calculate luminance (perceived brightness)
    double luminance = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;

    // Map luminance (0-255) to the character set index
    // 0 luminance (black) maps to the last char (space)
    // 255 luminance (white) maps to the first char ($)
    int index = static_cast<int>((luminance / 255.0) * (num_chars - 1) + 0.5); // Add 0.5 for rounding
    index = std::max(0, std::min(index, num_chars - 1)); // Clamp index

    // Return character from the set, adjusted for inverse mapping (darker = sparser, which is later in the string)
    return ascii_chars[num_chars - 1 - index];
}

// --- Helper function to compare buffers and generate update commands ---
std::vector<UpdateCommand> Renderer::generateUpdateCommands(int width, int height) {
    std::vector<UpdateCommand> update_commands;
    // Estimate capacity: reserve space for a fraction of cells, adjust if needed
    update_commands.reserve(static_cast<size_t>(width) * height / 4);

    // Get the current render mode from UI to determine comparison logic
    bool use_pixel_mode = ui_layer.isPixelMode();
    // Update the member variable for use elsewhere if needed (e.g., in Skybox or shade)
    // Although capturing it in the thread lambda is for thread safety,
    // the main thread logic here also needs the correct mode.
    use_background_color_mode = use_pixel_mode; // This flag seems to control non-UI background rendering
    ui_layer.setBackgroundColorMode(use_background_color_mode); // Also setting it in UI class

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Bounds check (should be fine if buffers are correctly sized)
            if (y >= static_cast<int>(current_buffer.size()) || x >= static_cast<int>(current_buffer[y].size()) ||
                y >= static_cast<int>(previous_buffer.size()) || x >= static_cast<int>(previous_buffer[y].size())) {
                 continue;
            }

            const ConsoleCell& current_cell = current_buffer[y][x];
            const ConsoleCell& previous_cell = previous_buffer[y][x];

            bool needs_update = false;
            // Force redraw on first frame or during loading/transition
            if (first_frame) { // is_loading check is handled before calling this
                needs_update = true;
            } else {
                 // Compare ConsoleCell including the new is_border flag
                needs_update = (current_cell != previous_cell);
            }

            if (needs_update) {
                // Create a command for this cell update
                UpdateCommand cmd;
                cmd.x = x;
                cmd.y = y;

                if (current_cell.is_ui) {
                    // Handle UI rendering based on the current render mode
                    if (use_pixel_mode) {
                        // In Pixel mode for UI, use is_border flag
                        if (current_cell.is_border) {
                            // Render border characters with colored background (pixeled effect)
                            cmd.character = ' '; // Use space character for solid block
                            cmd.target_fg.r = ANSI_COLOR_DEFAULT; // Signal default FG
                            cmd.target_fg.g = ANSI_COLOR_DEFAULT;
                            cmd.target_fg.b = ANSI_COLOR_DEFAULT;
                            cmd.target_bg = current_cell.color; // Use UI color as background
                        } else {
                            // Render non-border UI cells (text, internal spaces) with foreground color
                            cmd.character = current_cell.character; // Keep the original character
                            cmd.target_fg = current_cell.color; // Use original UI color for text foreground
                            // Explicitly set background color for UI text in pixel mode
                            cmd.target_bg = UI_BACKGROUND_COLOR; // Use the defined UI background color
                        }
                    } else {
                        // In ASCII mode for UI, use default foreground/background but keep character
                        // This removes the specific UI color from text/border, making it monochrome UI
                        cmd.character = current_cell.character;
                        cmd.target_fg = {255, 255, 255}; // White foreground (or another suitable default like {200, 200, 200})
                        cmd.target_bg = DEFAULT_BACKGROUND_COLOR; // Default background (usually black {0, 0, 0})
                    }
                } else {
                    // Non-UI elements (main rendered scene)
                    if (use_background_color_mode) { // This is true when in Pixel mode
                         // Pixel mode for non-UI (main render)
                        cmd.character = ' '; // Use space character for solid color
                        cmd.target_fg.r = ANSI_COLOR_DEFAULT; // Signal default FG (character is space)
                        cmd.target_fg.g = ANSI_COLOR_DEFAULT;
                        cmd.target_fg.b = ANSI_COLOR_DEFAULT;
                        cmd.target_bg = current_cell.color; // Use rendered color as background
                    } else { // This is true when in ASCII mode
                        // ASCII mode for non-UI (main render)
                        cmd.character = colorToAsciiChar(current_cell.color); // Character based on luminance
                        cmd.target_fg = {255, 255, 255}; // White foreground
                        cmd.target_bg = DEFAULT_BACKGROUND_COLOR; // Black background
                    }
                }
                update_commands.push_back(cmd);
            }
        }
    }
    return update_commands;
}

// --- Helper function to generate and output the final ANSI string ---
void Renderer::outputUpdateCommands(const std::vector<UpdateCommand>& update_commands, int height) {
    if (update_commands.empty()) {
        // If nothing changed, still flush (might be unnecessary but safe)
        std::cout << std::flush;
        return;
    }

        std::stringstream final_output_ss;
    // Estimate final string size (each command needs ~10-15 chars for cursor + colors + char)
    final_output_ss.str().reserve(update_commands.size() * 15);

    // Track current console state to minimize redundant ANSI codes
    RGBColor current_console_fg = {ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT};
    RGBColor current_console_bg = {ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT};
    int last_output_x = LAST_OUTPUT_X_INIT; // Tracks last written screen coordinate X
    int last_output_y = LAST_OUTPUT_Y_INIT; // Tracks last written screen coordinate Y

        for (const auto& cmd : update_commands) {
            // 1. Set Cursor Position if needed
        // Move cursor if it's not the very first character or not adjacent to the last one
            if (cmd.y != last_output_y || cmd.x != last_output_x + 1) {
            // ANSI cursor position is 1-indexed: Esc[Row;ColumnH
            final_output_ss << "\x1b[" << (cmd.y + ANSI_CURSOR_MOVE_BASE) << ";" << (cmd.x + ANSI_CURSOR_MOVE_BASE) << "H";
            // Reset known color state after cursor move, as the terminal's state is unknown
            current_console_fg = {ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT};
            current_console_bg = {ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT, ANSI_COLOR_DEFAULT};
            }

            // 2. Set Colors if changed from current console state
            if (cmd.target_fg != current_console_fg) {
            final_output_ss << colorToAnsiString(cmd.target_fg, true); // true for foreground
                current_console_fg = cmd.target_fg;
            }
            if (cmd.target_bg != current_console_bg) {
            final_output_ss << colorToAnsiString(cmd.target_bg, false); // false for background
                current_console_bg = cmd.target_bg;
            }

            // 3. Output Character
            final_output_ss << cmd.character;

            // Update last written position
            last_output_x = cmd.x;
            last_output_y = cmd.y;
        }

    // Append final reset and move cursor to bottom left to keep it out of the way
    final_output_ss << ANSI_RESET; // Full reset of colors and formatting
    final_output_ss << "\x1b[" << (height + ANSI_CURSOR_MOVE_BASE) << ";1H"; // Move cursor to the first column of the line after the buffer

    // Output the entire generated string at once for performance
        std::cout << final_output_ss.str() << std::flush;
}

// --- Main Rendering Method ---
void Renderer::render(const Scene& scene, int width, int height, const Vec3& camera_pos, const Vec3& camera_dir, const Vec3& camera_up, double fov, double fps, bool render_ui, bool is_loading) {

    // --- Initialize or Resize Persistent Buffers ---
    bool needs_clear = initializeOrResizeBuffers(width, height, is_loading);
    if (!needs_clear && (current_buffer.empty() || current_buffer[0].empty())) {
        // If initialization failed or resulted in empty buffers, stop rendering.
        // Error message is handled within initializeOrResizeBuffers.
        return;
    }

    // --- Clear Console (only if needed) ---
     if (needs_clear) {
          clearConsole(); // Assuming clearConsole sends the appropriate ANSI clear command
    }

    // --- Camera Setup ---
    const double aspect_ratio = static_cast<double>(width) / height;
    const double fov_radians = fov * M_PI / 180.0;
    const double half_view_height = std::tan(fov_radians / 2.0);
    const double half_view_width = half_view_height * aspect_ratio;
    const Vec3 cam_fwd = camera_dir.normalize(); // Ensure camera_dir is normalized
    const Vec3 cam_right = cam_fwd.cross(camera_up).normalize(); // Right vector must be orthogonal to forward and up
    const Vec3 cam_up = cam_right.cross(cam_fwd).normalize(); // Recalculate corrected Up vector

    // --- Raycasting and Scene Rendering OR Transition Effect ---
    // Perform raycasting if not in loading mode and not in transition
    if (!is_loading && render_mode_transition_frames <= 0) {
        // If Vulkan backend is enabled, we may offload computations here (future extension)
        if (use_vulkan_backend && vulkan) {
            // Calculate camera target from position and direction
            Vec3 camera_target = camera_pos + camera_dir.normalize();

            // Let VulkanRuntime handle buffer sizing internally; dispatch with scene data
            vulkan->processFrameWithScene(width, height, camera_pos, camera_target, camera_up, scene);
            // If Vulkan produced an output buffer, map it into current_buffer colors
            const uint32_t* gpuData = nullptr; uint32_t gw=0, gh=0;
            if (vulkan->getOutput(gpuData, gw, gh) && gpuData) {
                int copy_w = std::min(width, static_cast<int>(gw));
                int copy_h = std::min(height, static_cast<int>(gh));
                for (int y = 0; y < copy_h; ++y) {
                    for (int x = 0; x < copy_w; ++x) {
                        uint32_t rgba = gpuData[y * gw + x];
                        RGBColor c;
                        c.r = (rgba) & 0xFF;
                        c.g = (rgba >> 8) & 0xFF;
                        c.b = (rgba >> 16) & 0xFF;
                        current_buffer[y][x].color = c;
                        current_buffer[y][x].character = ' ';
                        current_buffer[y][x].is_ui = false;
                        current_buffer[y][x].is_border = false;
                    }
                }
            }
            // Update dynamic Vulkan stats in UI
            if (&ui_layer) {
                uint32_t ow=0, oh=0; vulkan->getOutputDims(ow, oh);
                ui_layer.setGpuDynamicStatus(ow, oh, vulkan->isPipelineReady(), vulkan->getLastDispatchMs());
            }
        } else {
            // --- Threading Setup - Improved Load Balancing ---
            unsigned int num_hardware_threads = std::thread::hardware_concurrency();
            if (num_hardware_threads == 0) num_hardware_threads = 1; // Ensure at least one thread
            // Use up to double the hardware concurrency or the number of rows, whichever is less
            unsigned int num_tasks = (height > 0) ? std::min(num_hardware_threads * 2, static_cast<unsigned int>(height)) : 0;
            if (num_tasks == 0 && height > 0) num_tasks = 1; // Ensure at least one task if height > 0

            // Structure to hold the future result and the starting row for each task
            struct TaskInfo {
                std::future<std::vector<std::vector<ConsoleCell>>> future;
                int start_row;
            };
            std::vector<TaskInfo> tasks(num_tasks);

            if (num_tasks > 0) {
                int rows_per_task = height / num_tasks;
                int remaining_rows = height % num_tasks;
                int current_start_row = 0;

                // Capture member variables by value for threads to ensure a consistent state
                Vec3 current_cloud_animation_offset_captured = cloud_animation_offset;
                double current_time_captured = current_time;
                bool current_show_normals_mode_captured = show_normals_mode;
                bool current_use_background_color_mode_captured = ui_layer.isPixelMode(); // Capture the mode state

                // --- Launch Tasks to Calculate Frame Data ---
                for (unsigned int i = 0; i < num_tasks; ++i) {
                    int task_rows = rows_per_task + (i < remaining_rows ? 1 : 0);
                    if (task_rows <= 0) continue;
                    int task_end_row = current_start_row + task_rows;
                    tasks[i].start_row = current_start_row;

                    // Launch the async task (lambda captures necessary variables by value or reference)
                    tasks[i].future = std::async(std::launch::async,
                        [&scene, width, height, camera_pos, cam_fwd, cam_up, cam_right, half_view_width, half_view_height, task_start_row = current_start_row, task_end_row,\
                         current_cloud_animation_offset_captured, current_time_captured, current_show_normals_mode_captured, current_use_background_color_mode_captured, this]() // Capture 'this' to call castRay
                        -> std::vector<std::vector<ConsoleCell>>
                        {
                            // Allocate buffer for this task's rows
                            std::vector<std::vector<ConsoleCell>> task_rows_buffer(task_end_row - task_start_row, std::vector<ConsoleCell>(width));
                            // Render the assigned rows
                            for (int y = task_start_row; y < task_end_row; ++y) {
                                int local_y = y - task_start_row;
                                for (int x = 0; x < width; ++x) {
                                    // Calculate normalized device coordinates [-1, 1]
                                    double ndc_x = (2.0 * (x + 0.5) / width - 1.0);
                                    double ndc_y = (1.0 - 2.0 * (y + 0.5) / height);
                                    // Calculate ray direction in world space
                                    Vec3 ray_direction = (cam_fwd
                                        + cam_right * (ndc_x * half_view_width)
                                        + cam_up * (ndc_y * half_view_height)
                                        ).normalize();
                                    Ray ray(camera_pos, ray_direction);
                                    // Call the member function castRay using 'this' pointer
                                    task_rows_buffer[local_y][x] = this->castRay(ray, scene); // castRay reads member variables which are captured
                                }
                            }
                            return task_rows_buffer; // Return the rendered rows for this task
                        });

                    current_start_row = task_end_row; // Set start row for the next task
                }
            }

            // --- Gather Results into current_buffer ---
            for (unsigned int i = 0; i < num_tasks; ++i) {
                if (!tasks[i].future.valid()) continue;
                try {
                    std::vector<std::vector<ConsoleCell>> task_result = tasks[i].future.get(); // Wait for task to complete and get result
                    int task_start_row = tasks[i].start_row;
                    // Copy the results into the main current_buffer
                    size_t current_buffer_row_index = task_start_row;
                    for (auto& row_data : task_result) {
                        if (current_buffer_row_index < current_buffer.size()) {
                            current_buffer[current_buffer_row_index] = std::move(row_data); // Use move assignment for efficiency
                            current_buffer_row_index++;
        } else {
                             ui_layer.addMessage("Warning: Attempted to write past end of render buffer during task result gathering", RGBColor{255, 150, 50});
                             break; // Prevent out-of-bounds write
                        }
                    }
                } catch (const std::future_error& e) {
                     ui_layer.addMessage("Warning: Render task future error: " + std::string(e.what()), RGBColor{255, 150, 50});
                } catch (const std::exception& e) {
                     ui_layer.addMessage("Warning: Render task exception: " + std::string(e.what()), RGBColor{255, 150, 50});
                }
            }
        }
    } else { // Use the static effect during loading OR transition
        // --- Apply Loading / Render Mode Transition Effect ---
        applyTransitionEffect(width, height); // Call helper function
    }

    // --- Render UI elements on top of the 3D scene (or blank background) ---
    // UI should render even if !show_ui during loading or transition to show progress/errors
    if (render_ui || is_loading || render_mode_transition_frames > 0) {
        ui_layer.render(current_buffer, width, height); // UI renders into the current_buffer
    }

    // --- Phase 1: Compare buffers and Generate Numeric Update Commands ---
    // This generates the list of cells that need to be updated on the console
    std::vector<UpdateCommand> update_commands = generateUpdateCommands(width, height); // Call helper function

    // --- Phase 2: Generate Final Output String from Numeric Commands and Output ---
    // This takes the update commands and creates an optimized ANSI escape sequence string to draw
    outputUpdateCommands(update_commands, height); // Call helper function

    // Mark frame as rendered only if not in loading mode
    if (!is_loading) {
    first_frame = false;
    }
}

void Renderer::toggleNormalsVisualization() {
    show_normals_mode = !show_normals_mode;
    // Reset first_frame to force a full redraw when mode changes
    first_frame = true;
    render_mode_transition_frames = TRANSITION_DURATION_FRAMES; // Start transition effect
}

void Renderer::toggleVulkanRendering() {
    use_vulkan_backend = !use_vulkan_backend;
    if (use_vulkan_backend) {
        if (!vulkan) {
            vulkan = std::make_unique<VulkanRuntime>();
        }
        if (!vulkan->initialize()) {
            // Initialization failed; fall back to CPU and inform UI
            use_vulkan_backend = false;
            if (&ui_layer) ui_layer.addMessage("Vulkan init failed. Falling back to CPU.", RGBColor{255, 100, 100});
        } else {
            if (&ui_layer) {
                std::string msg = "Vulkan GPU backend enabled";
                if (vulkan->hasEmbeddedShader()) msg += " (shader embedded)"; else msg += " (no shader embedded)";
                msg += ". Device: ";
                msg += vulkan->getDeviceName();
                ui_layer.addMessage(msg, RGBColor{100, 255, 100});
                ui_layer.setGpuStaticStatus(true, vulkan->hasEmbeddedShader(), vulkan->getDeviceName(),
                                           vulkan->getVendorId(), vulkan->getDeviceId(), vulkan->getApiVersion(),
                                           vulkan->getDriverVersion(), vulkan->getComputeQueueFamily());
            }
        }
    } else {
        if (vulkan) vulkan->shutdown();
        if (&ui_layer) ui_layer.addMessage("Vulkan GPU backend disabled (CPU only).", RGBColor{150, 150, 255});
        ui_layer.setGpuStaticStatus(false, false, "", 0, 0, 0, 0, 0);
    }
    first_frame = true;
    render_mode_transition_frames = TRANSITION_DURATION_FRAMES;
}

void Renderer::startTransition() {
    render_mode_transition_frames = TRANSITION_DURATION_FRAMES;
    first_frame = true; // Force redraw during transition
}

void Renderer::updateTransition() {
    if (render_mode_transition_frames > 0) {
        render_mode_transition_frames--;
         if (render_mode_transition_frames == 0) {
            // Transition ended, force another full redraw with the new mode
            first_frame = true;
        }
    }
}

void Renderer::updateAnimations(double deltaTime, double time_speed_multiplier, const Vec3& cloud_velocity) {
    // Update cloud animation offset
    cloud_animation_offset = cloud_animation_offset + cloud_velocity * deltaTime;

    // Wrap around offset to prevent large numbers, assuming a tiling texture or pattern
    // Use constant for wrap-around value
    if (cloud_animation_offset.x > CLOUD_OFFSET_WRAP_AROUND / 2.0) cloud_animation_offset.x -= CLOUD_OFFSET_WRAP_AROUND;
    if (cloud_animation_offset.x < -CLOUD_OFFSET_WRAP_AROUND / 2.0) cloud_animation_offset.x += CLOUD_OFFSET_WRAP_AROUND;
    if (cloud_animation_offset.y > CLOUD_OFFSET_WRAP_AROUND / 2.0) cloud_animation_offset.y -= CLOUD_OFFSET_WRAP_AROUND;
    if (cloud_animation_offset.y < -CLOUD_OFFSET_WRAP_AROUND / 2.0) cloud_animation_offset.y += CLOUD_OFFSET_WRAP_AROUND;
    if (cloud_animation_offset.z > CLOUD_OFFSET_WRAP_AROUND / 2.0) cloud_animation_offset.z -= CLOUD_OFFSET_WRAP_AROUND;
    if (cloud_animation_offset.z < -CLOUD_OFFSET_WRAP_AROUND / 2.0) cloud_animation_offset.z += CLOUD_OFFSET_WRAP_AROUND;


    // Update time of day (animate from 0 to 2*PI)
    current_time += (time_speed_multiplier * deltaTime); // Advance time
    // Wrap time between 0 and 2*PI for cyclical animation (e.g., sun position)
    if (current_time > 2.0 * M_PI) current_time -= 2.0 * M_PI;
    if (current_time < 0.0) current_time += 2.0 * M_PI;
}
