#define NOMINMAX
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#ifdef _WIN32 // Include Windows.h only on Windows
#include <Windows.h>
#else // Include necessary headers for Linux/Unix
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <memory>
#include <fstream> // For file I/O
#include <limits> // For std::numeric_limits
#include <sstream> // For parsing lines, stringstream in render
#include <string>  // For string manipulation
#include <map>     // For storing materials by name
#include <future>  // Required for std::async and std::future
#include <utility> // For std::pair
#include <iomanip> // Add this include for std::setw, std::left
#include <string_view> // Add for string replacement helper (maybe remove later)
#include <random> // Added for render mode transition effect
#include <chrono> // Added for seeding random number generator
#include <csignal> // Required for signal handling (Ctrl+C)

// --- Include New Headers ---
#include "src/constants.h" // Includes ANSI_RESET, MIN_CONSOLE_WIDTH, MIN_CONSOLE_HEIGHT, UI_STATUS_BAR_HEIGHT
#include "src/vector.h"
#include "src/ray.h"
#include "src/aabb.h"
#include "src/texture.h"
#include "src/material.h"
#include "src/object.h"
#include "src/triangle.h"
#include "src/light.h"
#include "src/bvh.h"
#include "src/scene.h"
#include "src/console.h" // Includes ConsoleCell with RGBColor
#include "src/collision_utils.h"
#include "src/player.h" // Include the new PlayerController header
#include "src/shading.h" // Includes shade and vec3ToColor
#include "src/ui.h" // Include new UI system
#include "src/noise.h" // Include the new noise header
#include "src/skybox.h" // Include the new skybox header
#include "src/console_utils.h" // Include the new console utilities header
#include "src/renderer.h" // Include the new Renderer header
#include "src/input.h" // Include the new InputManager header (Corrected include path)

// --- Global Signal Flag ---
// Use volatile sig_atomic_t for thread-safe flag in signal handler
volatile sig_atomic_t s_exit_requested = 0;

// --- Signal Handler ---
void handle_sigint(int signal) {
    s_exit_requested = 1; // Set flag to indicate exit request
}

// --- Scene Loading ---
bool loadSceneFromFile(const std::string& filename, Scene& scene,
                       Vec3& initial_pos, double& initial_yaw, double& initial_pitch, double& initial_fov, UI& ui_layer)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        ui_layer.addMessage("ERROR: Could not open scene file: " + filename, RGBColor{255, 50, 50});
        // std::cerr << "Error: Could not open scene file: " << filename << std::endl; // Removed duplicate output
        return false;
    }

    initial_pos = Vec3(0.0, 1.0, 0.0);
    initial_yaw = DEFAULT_PLAYER_YAW;
    initial_pitch = DEFAULT_PLAYER_PITCH;
    initial_fov = DEFAULT_FOV;

    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        line_number++;
        line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
        line.erase(line.find_last_not_of(" \t\n\r\f\v") + 1);
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string keyword;
        ss >> keyword;

        try {
            if (keyword == "material") {
                std::string name, symbol_str; char symbol; Vec3 color; double amb, diff, spec, shin;
                std::shared_ptr<Texture> texture = nullptr;

                ss >> name >> symbol_str >> color.x >> color.y >> color.z >> amb >> diff >> spec >> shin;
                if (ss.fail()) throw std::runtime_error("Invalid material numeric value format");
                symbol = symbol_str.length() > 0 ? symbol_str[0] : '?';

                std::string remaining_part; std::getline(ss, remaining_part); remaining_part.erase(0, remaining_part.find_first_not_of(" \t"));
                if (!remaining_part.empty() && remaining_part.rfind("texture:", 0) == 0) {
                    std::string tex_filename = remaining_part.substr(8);
                    texture = Texture::load(tex_filename, ui_layer);
                    if (!texture) {
                            ui_layer.addMessage("Warning [Line " + std::to_string(line_number) + "]: Failed to load texture '" + tex_filename + "' for material '" + name + "'. Symbol will be used.", RGBColor{255, 150, 50});
                            // std::cerr << "Warning [Line " << line_number << "]: Failed to load texture '" << tex_filename + "' for material '" + name + "'. Symbol will be used." << std::endl; // Removed duplicate output
                    }
                }
                scene.materials[name] = Material(symbol, color, amb, diff, spec, shin, texture);
            } else if (keyword == "light") {
                Vec3 pos, color; double intensity;
                ss >> pos.x >> pos.y >> pos.z >> color.x >> color.y >> color.z >> intensity;
                if (ss.fail()) throw std::runtime_error("Invalid light numeric value format");
                scene.lights.push_back(Light(pos, color, intensity));
            } else if (keyword == "triangle") {
                Vec3 v[3]; Vec2 uv[3]; std::string mat_name;
                ss >> v[0].x >> v[0].y >> v[0].z >> v[1].x >> v[1].y >> v[1].z >> v[2].x >> v[2].y >> v[2].z >> uv[0].u >> uv[0].v >> uv[1].u >> uv[1].v >> uv[2].u >> uv[2].v >> mat_name;
                if (ss.fail()) throw std::runtime_error("Invalid triangle numeric value format or missing material name");
                auto mat_it = scene.materials.find(mat_name); if (mat_it == scene.materials.end()) throw std::runtime_error("Material '" + mat_name + "' not found for triangle");
                scene.objects.push_back(std::make_unique<Triangle>(v[0], v[1], v[2], uv[0], uv[1], uv[2], mat_it->second));
            } else if (keyword == "quad") {
                Vec3 v[4]; Vec2 uv[4]; std::string mat_name;
                ss >> v[0].x >> v[0].y >> v[0].z >> v[1].x >> v[1].y >> v[1].z >> v[2].x >> v[2].y >> v[2].z >> v[3].x >> v[3].y >> v[3].z >> uv[0].u >> uv[0].v >> uv[1].u >> uv[1].v >> uv[2].u >> uv[2].v >> uv[3].u >> uv[3].v >> mat_name;
                if (ss.fail()) throw std::runtime_error("Invalid quad numeric value format or missing material name");
                auto mat_it = scene.materials.find(mat_name); if (mat_it == scene.materials.end()) throw std::runtime_error("Material '" + mat_name + "' not found for quad");
                scene.addQuad(v[0], v[1], v[2], v[3], uv[0], uv[1], uv[2], uv[3], mat_it->second);
            } else if (keyword == "camera_pos") { ss >> initial_pos.x >> initial_pos.y >> initial_pos.z; if (ss.fail()) throw std::runtime_error("Invalid camera_pos numeric value format");
            } else if (keyword == "camera_angles") { ss >> initial_yaw >> initial_pitch; if (ss.fail()) throw std::runtime_error("Invalid camera_angles numeric value format (expected yaw pitch)");
            } else if (keyword == "camera_fov") {
                ss >> initial_fov;
                if (ss.fail()) throw std::runtime_error("Invalid camera_fov numeric value format");
            } else {
                 ui_layer.addMessage("Warning [Line " + std::to_string(line_number) + "]: Unknown keyword '" + keyword + "'", RGBColor{255, 150, 50});
                 // std::cerr << "Warning [Line " << line_number << "]: Unknown keyword '" << keyword << "'" << std::endl; // Removed duplicate output
            }
        }
        catch (const std::exception& e) {
            ui_layer.addMessage("Error parsing scene file [Line " + std::to_string(line_number) + "]: " + e.what(), RGBColor{255, 50, 50});
            // std::cerr << "Error parsing scene file [Line " << std::to_string(line_number) << "]: " << e.what() << std::endl; // Removed duplicate output
            // std::cerr << "   Line content: " << line << std::endl; // Removed duplicate output
            file.close();
            return false;
        }
    }
    file.close();
    ui_layer.addMessage("Scene loaded successfully from " + filename, RGBColor{100, 255, 100});
    ui_layer.addMessage("Stats: Materials=" + std::to_string(scene.materials.size()) + ", Lights=" + std::to_string(scene.lights.size()) + ", Objects=" + std::to_string(scene.objects.size()), RGBColor{150, 150, 255});
    return true;
}


// --- Game Class ---
class Game {
public:
    Game(); // Constructor

    // Initialize the game (load scene, setup players, etc.)
    bool initialize();

    // Run the main game loop
    void run();

    // Console cleanup for main()
    void restoreConsole();


private:
    // Core engine components
    UI ui_layer; // UI layer is managed by the Game class
    Renderer renderer;
    InputManager input_manager;
    PlayerController player_controller;
    Scene scene; // The scene data

    // Game state variables
    bool is_paused = false;
    int width = 0, height = 0; // Console dimensions

    // FPS tracking
    int fps_counter = 0;
    double fps_timer = 0.0;
    double current_fps = 0.0;

    // Private helper methods
    bool showLoadingScreen(); // Renamed and made private

    // Console setup/cleanup
    void setupConsole();
#ifdef _WIN32 // centerCursor is Windows-specific
    // void centerCursor(); // Removed
#endif
};

// --- Game Class Implementation ---

Game::Game() : renderer(ui_layer) {
    // Constructor: Initialize member objects.
    // ui_layer is implicitly initialized.
    // renderer is initialized with ui_layer reference.
    // input_manager is implicitly initialized.
    // player_controller is implicitly initialized.
    // scene is implicitly initialized.
}

bool Game::initialize() {
    setupConsole();

    if (!showLoadingScreen()) {
        restoreConsole(); // Restore console before exiting on failure
        return false;
    }

    // Initial values loaded during showLoadingScreen
    // PlayerController is initialized within showLoadingScreen now.

    // Add initial UI messages after loading is complete
    ui_layer.addMessage("Console 3D Engine started.", RGBColor{100, 255, 100});
    ui_layer.addMessage("Press 1 to toggle UI display.", RGBColor{255, 255, 100});
    ui_layer.addMessage("Press 2 to toggle render mode (ASCII/Pixel).", RGBColor{255, 255, 100});
    ui_layer.addMessage("Press 3 to toggle Normals visualization.", RGBColor{255, 255, 100});
    ui_layer.addMessage("Press 4 to toggle Vulkan rendering (GPU/CPU).", RGBColor{255, 255, 100});

#ifdef _WIN32 // centerCursor is Windows-specific
    // centerCursor(); // Removed
#endif

    return true;
}

void Game::run() {
    const double targetFrameTime = 1.0 / TARGET_FPS;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (!s_exit_requested) { // Loop while exit is NOT requested
        auto frameStartTime = std::chrono::high_resolution_clock::now();
        double deltaTime = std::chrono::duration<double>(frameStartTime - lastFrameTime).count();
        lastFrameTime = frameStartTime;
        deltaTime = std::min(deltaTime, targetFrameTime * MAX_DELTA_TIME_SCALE);

        // --- FPS Calculation ---
        fps_timer += deltaTime;
        fps_counter++;
        if (fps_timer >= 0.5) {
            current_fps = fps_counter / fps_timer;
            fps_counter = 0;
            fps_timer = 0.0;
        }

        // --- Input ---
        input_manager.update();

        // --- Pause ---
        if (input_manager.isPauseToggled()) {
            is_paused = !is_paused;
            setCursorVisibility(is_paused);
            if (is_paused) {
                 clearConsole();
                 setCursorPosition(width / 2 - 3, height / 2);
                 std::cout << ANSI_RESET << "\x1b[44;37m PAUSED \x1b[0m" << std::flush;
             } else {
                clearConsole();
#ifdef _WIN32 // centerCursor is Windows-specific
                // centerCursor(); // Removed
#endif
                lastFrameTime = std::chrono::high_resolution_clock::now(); // Prevent large deltaTime
                deltaTime = 0;
            }
        }

        // --- UI Toggle ---
        if (input_manager.isUIToggled() && !is_paused) {
            ui_layer.toggleVisibility(); // UI class now manages its visibility
            // Renderer doesn't need show_ui flag explicitly anymore
        }

        // --- Render Mode Toggle (ASCII/PIXEL) ---
        if (input_manager.isRenderModeToggled() && !is_paused && !renderer.isInTransition()) {
            ui_layer.toggleRenderMode(); // UI class manages render mode
            renderer.startTransition();

            if (ui_layer.isPixelMode()) { // Check UI for current mode
                ui_layer.addMessage("Render Mode: Pixel", RGBColor{100, 255, 100});
            } else {
                ui_layer.addMessage("Render Mode: ASCII", RGBColor{100, 100, 255});
            }
        }

        // --- Normals Visualization Toggle (F5) ---
        if (input_manager.isNormalsTogglePressed() && !is_paused) {
            renderer.toggleNormalsVisualization();

            if (renderer.isNormalsVisualizationEnabled()) {
                ui_layer.addMessage("Render Mode: Normals Visualization", RGBColor{100, 255, 255});
            } else {
                 if (ui_layer.isPixelMode()) {
                    ui_layer.addMessage("Render Mode: Pixel", RGBColor{100, 255, 100});
                } else {
                    ui_layer.addMessage("Render Mode: ASCII", RGBColor{100, 100, 255});
                }
            }
        }

        // --- Vulkan Rendering Toggle ---
        if (input_manager.isGpuTogglePressed() && !is_paused && !renderer.isInTransition()) {
            renderer.toggleVulkanRendering();
        }

        // --- Dynamic Resolution ---
#ifdef _WIN32 // Windows Implementation
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int new_width = width, new_height = height;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            new_width = std::max(MIN_CONSOLE_WIDTH, (int)csbi.dwSize.X);
            new_height = std::max(MIN_CONSOLE_HEIGHT, (int)csbi.dwSize.Y - 1); // -1 to avoid scroll usually
            if (new_height <= 0) new_height = 1;
        }
#else // Linux/Unix Implementation
        struct winsize ws;
        int new_width = width, new_height = height;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
            new_width = std::max(MIN_CONSOLE_WIDTH, (int)ws.ws_col);
            new_height = std::max(MIN_CONSOLE_HEIGHT, (int)ws.ws_row - 1); // -1 to avoid scroll usually
            if (new_height <= 0) new_height = 1;
        }
#endif
        if (new_width != width || new_height != height) {
            width = new_width;
            height = new_height;

            std::stringstream res_msg;
            res_msg << "Resolution changed to " << width << "x" << height;
            ui_layer.addMessage(res_msg.str(), RGBColor{150, 150, 255});

            ui_layer.handleResize(width, height);
            // Renderer handles its buffer resize internally in its render method
            // based on the width/height passed to it.

            if (is_paused) {
                 clearConsole();
                 setCursorPosition(width / 2 - 3, height / 2);
                 std::cout << ANSI_RESET << "\x1b[44;37m PAUSED \x1b[0m" << std::flush;
             } else {
                clearConsole();
             }
        }

        // --- Game Logic (Not Paused) ---
        if (!is_paused) {
            player_controller.update(deltaTime, input_manager, scene);

            Vec3 cloud_velocity(SKYBOX_CLOUD_ANIMATION_VELOCITY_X, SKYBOX_CLOUD_ANIMATION_VELOCITY_Y, SKYBOX_CLOUD_ANIMATION_VELOCITY_Z);
            renderer.updateAnimations(deltaTime, TIME_SPEED_MULTIPLIER, cloud_velocity);
            renderer.updateTransition();

            Vec3 camera_pos = player_controller.getCameraPosition();
            Vec3 camera_dir = player_controller.getCameraDirection();
            Vec3 camera_up = player_controller.getCameraUp();
            double camera_fov = DEFAULT_FOV; // Assuming FOV is constant

            // Render
            // Pass UI visibility state from ui_layer
            renderer.render(scene, width, height, camera_pos, camera_dir, camera_up, camera_fov, current_fps, ui_layer.isVisible(), false);

            // Update UI after rendering so it has the latest FPS and other info
            ui_layer.update(deltaTime, player_controller.getPosition(), player_controller.getCameraDirection(), current_fps);
            ui_layer.setTriangleCount(scene.objects.size());


        } else {
            // Paused State - Only update and render UI
            Vec3 paused_pos = player_controller.getPosition(); // Still show UI at last known position
            Vec3 paused_dir = player_controller.getCameraDirection();
            ui_layer.update(deltaTime, paused_pos, paused_dir, current_fps);

            Scene paused_scene; // Empty scene for rendering in paused state
            Vec3 temp_paused_pos = {0,0,0}; Vec3 temp_paused_dir = {0,0,-1}; Vec3 temp_paused_up = {0,1,0}; double temp_paused_fov = 90.0; double temp_paused_fps = 0.0;

            // Render UI on top of a potentially blank/paused background
            // Pass UI visibility state even when paused
            renderer.render(paused_scene, width, height, temp_paused_pos, temp_paused_dir, temp_paused_up, temp_paused_fov, ui_layer.isVisible(), true); // Pass is_loading=true to suppress raycasting
        }

        // Frame Timing / Sleep
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        double frameDurationSeconds = std::chrono::duration<double>(frameEndTime - frameStartTime).count();
        double sleepDurationSeconds = targetFrameTime - frameDurationSeconds;
        if (sleepDurationSeconds > 0) {
            auto sleepDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(sleepDurationSeconds));
            if (sleepDurationMs.count() > 1) {
                std::this_thread::sleep_for(sleepDurationMs);
            }
        }
    }
}

// --- Console Setup/Cleanup (Platform-specific) ---
void Game::setupConsole() {
#ifdef _WIN32 // Windows Implementation
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            width = csbi.dwSize.X;
            height = csbi.dwSize.Y > 1 ? csbi.dwSize.Y - 1 : 1; // -1 to avoid scroll usually
            width = std::max(MIN_CONSOLE_WIDTH, width);
            height = std::max(MIN_CONSOLE_HEIGHT, height);
        }
    }
    setCursorVisibility(false);

    if (hOut == INVALID_HANDLE_VALUE) {
        ui_layer.addMessage("Error: Invalid standard output handle.", RGBColor{255, 50, 50});
        // Should ideally exit or handle gracefully, but sticking to refactoring main for now.
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
         ui_layer.addMessage("Error: Failed to get console mode.", RGBColor{255, 50, 50});
        // Should ideally exit or handle gracefully
    }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode)) {
        ui_layer.addMessage("Warning: Failed to set console mode (ENABLE_VIRTUAL_TERMINAL_PROCESSING).", RGBColor{255, 150, 50});
    }
#else // Linux/Unix Implementation
    // For Linux, we primarily rely on ANSI escape codes for output.
    // We need to get initial terminal size.
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        width = std::max(MIN_CONSOLE_WIDTH, (int)ws.ws_col);
        height = std::max(MIN_CONSOLE_HEIGHT, (int)ws.ws_row - 1); // -1 to avoid scroll usually
        if (height <= 0) height = 1;
    } else {
        // Fallback if ioctl fails (shouldn't happen on most terminals)
        width = MIN_CONSOLE_WIDTH;
        height = MIN_CONSOLE_HEIGHT;
        ui_layer.addMessage("Warning: Could not get terminal size via ioctl. Using fallback minimum size.", RGBColor{255, 150, 50});
    }
    setCursorVisibility(false); // Hide cursor on Linux
#endif

    ui_layer.handleResize(width, height); // Inform UI of initial dimensions
}

void Game::restoreConsole() {
    setCursorVisibility(true);
    std::cout << ANSI_RESET; // Keep console reset
}

#ifdef _WIN32 // centerCursor is Windows-specific
// void Game::centerCursor() { // Removed
//     HWND consoleWindow = GetConsoleWindow(); // Removed
//     if (consoleWindow) { // Removed
//         RECT clientRect; // Removed
//         if (GetClientRect(consoleWindow, &clientRect)) { // Removed
//             POINT center = { (clientRect.left + clientRect.right) / 2, (clientRect.top + clientRect.bottom) / 2 }; // Removed
//             ClientToScreen(consoleWindow, &center); // Removed
//             SetCursorPos(center.x, center.y); // Removed
//         } // Removed
//     } // Removed
// } // Removed
#endif

bool Game::showLoadingScreen() {
    clearConsole(); // Ensure console is cleared at the start of loading
    setCursorVisibility(false);

    const int min_required_width = MIN_CONSOLE_WIDTH;
    const int min_required_height = MIN_CONSOLE_HEIGHT;

    if (width < min_required_width || height < min_required_height) {
        setCursorPosition(0, 0);
        // Keep this specific error message in cerr as it happens before UI is fully functional
        std::cerr << ANSI_RESET << "\x1b[31mERROR: Console window too small (" << width << "x" << height << "). Minimum required: " << min_required_width << "x" << min_required_height << ". Please resize and restart.\x1b[0m" << std::endl;
        setCursorVisibility(true);
        std::cout << "\nPress Enter to exit..." << std::endl; // Keep exit prompt in cout
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
        return false;
    }

    ui_layer.handleResize(width, height);
    ui_layer.setBackgroundColorMode(false);

    Vec3 temp_pos = {0,0,0}; Vec3 temp_dir = {0,0,-1}; Vec3 temp_up = {0,1,0}; double temp_fov = 90.0; double temp_fps = 0.0;
    Scene temp_scene; // Empty scene for rendering during loading

    // Pass render_ui = true and is_loading = true
    renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    if (s_exit_requested) return false; // Check flag during loading

    ui_layer.addMessage("Parsing scene file: scene.txt...", RGBColor{200, 200, 100});
    // Pass render_ui = true and is_loading = true
    renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);

    // Variables to be populated by loadSceneFromFile
    Vec3 loaded_initial_pos;
    double loaded_initial_yaw, loaded_initial_pitch, loaded_initial_fov;

    if (!::loadSceneFromFile("scene.txt", scene, loaded_initial_pos, loaded_initial_yaw, loaded_initial_pitch, loaded_initial_fov, ui_layer)) { // Call the global function
         // Pass render_ui = true and is_loading = true
         renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);
         setCursorVisibility(true);
         // Keep this specific error message in cerr as it's a critical failure
         std::cerr << "\nError loading scene file. Details above." << std::endl;
         std::cout << "\nPress Enter to exit..." << std::endl; // Keep exit prompt in cout
         std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
         std::cin.get();
         return false;
    }
    if (s_exit_requested) return false; // Check flag during loading

    // Initialize player controller after scene is loaded and initial state is known
    player_controller.initialize(loaded_initial_pos, loaded_initial_yaw, loaded_initial_pitch);
    // Assuming camera_fov is constant, if not, need to store and use loaded_initial_fov

    // Pass render_ui = true and is_loading = true
    renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
     if (s_exit_requested) return false; // Check flag during loading


    ui_layer.addMessage("Building acceleration structure (BVH)...", RGBColor{200, 150, 100});
    // Pass render_ui = true and is_loading = true
    renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);
    scene.buildAccelerationStructure();
    ui_layer.addMessage("BVH built successfully.", RGBColor{100, 200, 100});
    // Pass render_ui = true and is_loading = true
    renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    if (s_exit_requested) return false; // Check flag during loading


    ui_layer.addMessage("Loading complete! Starting engine...", RGBColor{100, 255, 100});
    // Pass render_ui = true and is_loading = true
    renderer.render(temp_scene, width, height, temp_pos, temp_dir, temp_up, temp_fov, temp_fps, true, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    if (s_exit_requested) return false; // Check flag during loading


    clearConsole(); // Clear loading UI before starting the main game loop render

    return true;
}


// --- Main Function ---
int main() {
    Game game; // Instantiate the Game object

    // Register the signal handler for SIGINT (Ctrl+C)
    std::signal(SIGINT, handle_sigint);

    if (!game.initialize()) {
        game.restoreConsole(); // Ensure console is restored even if initialization fails
        clearConsole(); // Add clear on failed init as well
        return 1; // Exit if initialization failed
    }

    game.run(); // Run the main game loop (now terminates on s_exit_requested)

    game.restoreConsole(); // Restore console settings on exit (either normal or via signal)
    clearConsole(); // Add one more clear after restore

    return 0;
}