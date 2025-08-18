#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <deque>
#include "console.h"
#include "vector.h"

// Enum for different UI elements/windows
enum class UIElementType {
    DEBUG_STATS,      // FPS, position, direction, etc.
    MESSAGE_LOG,      // Game messages/events
    CUSTOM_TEXT,      // For any custom text display
    TITLE_BAR,        // Full-width title bar at the top
    VULKAN_STATS      // Vulkan backend status box
};

// Struct for UI element position and size
struct UIElement {
    UIElementType type;
    int x, y;           // Top-left position
    int width, height;  // Dimensions
    bool visible;       // Whether this element is visible
    
    UIElement(UIElementType t, int _x, int _y, int w, int h, bool vis = true)
        : type(t), x(_x), y(_y), width(w), height(h), visible(vis) {}
};

// Class representing a message in the log
struct UIMessage {
    std::string text;
    RGBColor color;
    std::chrono::steady_clock::time_point timestamp;
    
    UIMessage(const std::string& msg, const RGBColor& col = RGBColor{200, 200, 200})
        : text(msg), color(col), timestamp(std::chrono::steady_clock::now()) {}
        
    // Comparison operator for finding messages in the queue
    bool operator==(const UIMessage& other) const {
        return text == other.text && 
               color == other.color && 
               std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - other.timestamp).count() == 0;
    }
};

// Main UI class to handle all overlay elements
class UI {
private:
    std::vector<UIElement> elements;
    std::deque<UIMessage> message_log;
    static const size_t MAX_MESSAGES = 5;
    std::chrono::steady_clock::time_point last_fps_update;
    double fps_display_value;
    int frame_counter;
    
    // State variables for display
    Vec3 current_player_pos;
    Vec3 current_camera_dir;
    int triangle_count;
    bool background_color_mode;
    bool pixel_mode; // Flag to track whether we're in ASCII or PIXEL mode
    bool global_ui_visible = true;
    std::string app_title;
    std::string app_version;

    // Vulkan status state
    bool vulkan_enabled = false;
    bool vulkan_shader_embedded = false;
    std::string vulkan_device_name;
    uint32_t vulkan_vendor_id = 0;
    uint32_t vulkan_device_id = 0;
    uint32_t vulkan_api_version = 0;
    uint32_t vulkan_driver_version = 0;
    uint32_t vulkan_queue_family = 0;
    uint32_t vulkan_out_w = 0;
    uint32_t vulkan_out_h = 0;
    bool vulkan_pipeline_ready = false;
    double vulkan_last_dispatch_ms = 0.0;
    
    void recalculateElementPositions(int console_width, int console_height);
    
    // Helper methods for rendering specific UI elements
    void drawBox(std::vector<std::vector<ConsoleCell>>& buffer, int x, int y, int width, int height, const std::string& title = "", bool fill_inner = true);
    void renderDebugStats(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element);
    void renderMessageLog(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element);
    void renderTitleBar(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element);
    void renderVulkanStats(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element);
    static std::string formatVersion(uint32_t ver);

public:
    UI();
    
    void addElement(UIElementType type, int x, int y, int width, int height, bool visible = true);
    void addMessage(const std::string& message, const RGBColor& color = RGBColor{200, 200, 200});
    void update(double deltaTime, const Vec3& player_pos, const Vec3& camera_dir, double fps);
    void setTriangleCount(int count) { triangle_count = count; }
    void setBackgroundColorMode(bool mode) { background_color_mode = mode; }
    void render(std::vector<std::vector<ConsoleCell>>& buffer, int console_width, int console_height);
    void toggleElement(UIElementType type);
    void handleResize(int new_width, int new_height);
    void toggleRenderMode() { pixel_mode = !pixel_mode; }
    bool isPixelMode() const { return pixel_mode; }
    void toggleVisibility() { global_ui_visible = !global_ui_visible; }
    bool isVisible() const { return global_ui_visible; }

    // Setters for Vulkan status
    void setGpuStaticStatus(bool enabled, bool shader_embedded, const std::string& device_name,
                            uint32_t vendor_id, uint32_t device_id, uint32_t api_version,
                            uint32_t driver_version, uint32_t queue_family);
    void setGpuDynamicStatus(uint32_t out_w, uint32_t out_h, bool pipeline_ready, double last_ms);
};