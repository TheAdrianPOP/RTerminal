#include "ui.h"
#include "constants.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <vector>

UI::UI() 
    : fps_display_value(0.0), frame_counter(0), triangle_count(0), 
      background_color_mode(false), pixel_mode(false), global_ui_visible(true),
      app_title("Console Ray Tracer"), app_version("v0.3.0") 
{
    last_fps_update = std::chrono::steady_clock::now();
    // Initialize default UI elements, including the new TITLE_BAR
    addElement(UIElementType::TITLE_BAR, 0, 0, 1, 1, true); 
    addElement(UIElementType::DEBUG_STATS, 0, 0, 30, 8);
    addElement(UIElementType::MESSAGE_LOG, 0, 9, 80, MAX_MESSAGES + 2);
    addElement(UIElementType::VULKAN_STATS, 0, 0, 48, 12);
}

void UI::addElement(UIElementType type, int x, int y, int width, int height, bool visible) {
    elements.emplace_back(type, x, y, width, height, visible);
}

void UI::addMessage(const std::string& message, const RGBColor& color) {
    if (message_log.size() >= MAX_MESSAGES) {
        message_log.pop_front();
    }
    message_log.emplace_back(message, color);
}

void UI::update(double deltaTime, const Vec3& player_pos, const Vec3& camera_dir, double fps) {
    current_player_pos = player_pos;
    current_camera_dir = camera_dir;

    frame_counter++;
    auto now = std::chrono::steady_clock::now();
    double elapsed_since_last_fps_update = std::chrono::duration<double>(now - last_fps_update).count();

    if (elapsed_since_last_fps_update >= 0.5) {
        fps_display_value = fps;
        frame_counter = 0;
        last_fps_update = now;
    }
}

void UI::render(std::vector<std::vector<ConsoleCell>>& buffer, int console_width, int console_height) {
    if (buffer.empty() || buffer[0].empty()) return;

    for (const auto& element : elements) {
        if (!element.visible) continue;

        if (element.x < 0 || element.y < 0 ||
            element.x + element.width > console_width ||
            element.y + element.height > console_height) {
            continue;
        }

        switch (element.type) {
            case UIElementType::DEBUG_STATS:
                renderDebugStats(buffer, element);
                break;
            case UIElementType::MESSAGE_LOG:
                renderMessageLog(buffer, element);
                break;
            case UIElementType::TITLE_BAR:
                renderTitleBar(buffer, element);
                break;
            case UIElementType::VULKAN_STATS:
                renderVulkanStats(buffer, element);
                break;
            default:
                break;
        }
    }
}

void UI::toggleElement(UIElementType type) {
    for (auto& element : elements) {
        if (element.type == type) {
            element.visible = !element.visible;
        }
    }
}

void UI::handleResize(int new_width, int new_height) {
    recalculateElementPositions(new_width, new_height);
}

void UI::recalculateElementPositions(int console_width, int console_height) {
    for (auto& element : elements) {
        switch (element.type) {
            case UIElementType::DEBUG_STATS:
                element.x = 1;
                element.y = 2; // Positioned below the title bar
                element.width = std::min(40, console_width - 2);
                element.height = std::min(8, console_height - 3);
                break;
            case UIElementType::MESSAGE_LOG:
                element.x = 1;
                element.y = std::min(11, console_height / 2); // Positioned below other elements
                element.width = std::min(80, console_width - 2);
                element.height = std::min(static_cast<int>(MAX_MESSAGES) + 2, console_height - element.y - 1);
                break;
            case UIElementType::VULKAN_STATS: {
                int w = 48;
                int h = 12;
                element.width = std::min(w, console_width - 2);
                element.height = std::min(h, console_height - 3);
                element.x = std::max(1, console_width - element.width - 1);
                element.y = 2; // Positioned below the title bar
                break;
            }
            case UIElementType::TITLE_BAR:
                element.x = 0;
                element.y = 0;
                element.width = console_width;
                element.height = 1;
                break;
            default:
                break;
        }
    }
}

void UI::drawBox(std::vector<std::vector<ConsoleCell>>& buffer, int x, int y, int width, int height, const std::string& title, bool fill_inner) {
    if (width <= 0 || height <= 0) return;

    RGBColor border_color = {80, 110, 150};
    RGBColor title_color = {220, 220, 255};
    RGBColor fill_color = {25, 35, 50};
    RGBColor shadow_color = {15, 15, 15};

    // --- Draw Drop Shadow ---
    for (int r = y + 1; r < y + height + 1; ++r) {
        if (r <= 0 || r >= static_cast<int>(buffer.size())) continue;
        for (int c = x + 1; c < x + width + 1; ++c) {
            if (c <= 0 || c >= static_cast<int>(buffer[0].size())) continue;
            if (!buffer[r][c].is_ui || buffer[r][c].is_border) {
                buffer[r][c].color = shadow_color;
                buffer[r][c].character = ' ';
            }
        }
    }
    
    // --- Draw Box Borders and Fill ---
    for (int r = y; r < y + height; ++r) {
        if (r < 0 || r >= static_cast<int>(buffer.size())) continue;
        for (int c = x; c < x + width; ++c) {
            if (c < 0 || c >= static_cast<int>(buffer[0].size())) continue;
            
            bool is_border_cell = (r == y || r == y + height - 1 || c == x || c == x + width - 1);

            if (is_border_cell) {
                buffer[r][c].color = border_color;
                buffer[r][c].is_border = true;
                if (r == y && c == x) buffer[r][c].character = '+';
                else if (r == y && c == x + width - 1) buffer[r][c].character = '+';
                else if (r == y + height - 1 && c == x) buffer[r][c].character = '+';
                else if (r == y + height - 1 && c == x + width - 1) buffer[r][c].character = '+';
                else if (r == y || r == y + height - 1) buffer[r][c].character = '-';
                else buffer[r][c].character = '|';
            } else if (fill_inner) {
                buffer[r][c].character = ' ';
                buffer[r][c].color = fill_color;
                buffer[r][c].is_border = false;
            }
            buffer[r][c].is_ui = true;
        }
    }

    // --- Draw Title on Top Border ---
    if (!title.empty() && width > 4 && height > 1) {
        std::string full_title = " " + title + " ";
        int title_start_pos = x + (width - full_title.length()) / 2;
        for (size_t i = 0; i < full_title.length(); ++i) {
            int current_col = title_start_pos + i;
            if (current_col > x && current_col < x + width - 1) {
                buffer[y][current_col].character = full_title[i];
                buffer[y][current_col].color = title_color;
                buffer[y][current_col].is_border = true;
                buffer[y][current_col].is_ui = true;
            }
        }
    }
}

void UI::renderTitleBar(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element) {
    RGBColor bar_bg_color = {20, 25, 40};
    RGBColor title_color = {210, 210, 255};
    RGBColor version_color = {150, 160, 180};

    // 1. Fill the background of the entire bar
    for (int c = element.x; c < element.x + element.width; ++c) {
        if (element.y >= 0 && element.y < static_cast<int>(buffer.size()) && c >=0 && c < static_cast<int>(buffer[element.y].size())) {
            buffer[element.y][c].character = ' ';
            buffer[element.y][c].color = bar_bg_color;
            buffer[element.y][c].is_ui = true;
        }
    }

    // 2. Render the application title on the left
    std::string title_text = " " + app_title + " ";
    int title_x = element.x + 1;
    for (size_t i = 0; i < title_text.length() && (title_x + static_cast<int>(i) < element.x + element.width); ++i) {
        buffer[element.y][title_x + i].character = title_text[i];
        buffer[element.y][title_x + i].color = title_color;
    }

    // 3. Render the version number on the right
    std::string version_text = " " + app_version + " ";
    int version_x = element.x + element.width - version_text.length() - 1;
    if(version_x > title_x + static_cast<int>(title_text.length())) { // Prevent overlap
        for (size_t i = 0; i < version_text.length(); ++i) {
            int current_x = version_x + i;
            if (current_x < element.x + element.width) {
                 buffer[element.y][current_x].character = version_text[i];
                 buffer[element.y][current_x].color = version_color;
            }
        }
    }
}


// ... other render functions (renderDebugStats, renderMessageLog, renderVulkanStats) remain the same ...
// ... UI utility functions (formatVersion, etc.) also remain the same ...

void UI::renderVulkanStats(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element) {
    drawBox(buffer, element.x, element.y, element.width, element.height, "Vulkan Stats");

    RGBColor text_color = {180, 220, 255};
    RGBColor header_color = {200, 255, 200};
    std::vector<std::pair<std::string, std::string>> lines;

    lines.push_back({"Vulkan", vulkan_enabled ? "ON" : "OFF"});
    lines.push_back({"Shader", vulkan_shader_embedded ? "Embedded" : "N/A"});
    lines.push_back({"Device", vulkan_device_name.empty() ? "-" : vulkan_device_name});

    if (vulkan_enabled) {
        std::stringstream s1, s2, s3, s4, s5;
        s1 << "0x" << std::hex << vulkan_vendor_id << " / 0x" << vulkan_device_id;
        s2 << formatVersion(vulkan_api_version) << " / 0x" << std::hex << vulkan_driver_version;
        s3 << std::dec << vulkan_queue_family;
        s4 << vulkan_out_w << "x" << vulkan_out_h << " (" << (vulkan_pipeline_ready ? "Ready" : "Wait") << ")";
        s5 << std::fixed << std::setprecision(3) << vulkan_last_dispatch_ms << " ms";

        lines.push_back({"ID (V/D)", s1.str()});
        lines.push_back({"Ver (API/Drv)", s2.str()});
        lines.push_back({"Queue Family", s3.str()});
        lines.push_back({"Output", s4.str()});
        lines.push_back({"Dispatch", s5.str()});
    }

    int line_y = element.y + 1;
    for (const auto& line_pair : lines) {
        if (line_y >= element.y + element.height - 1) break;
        int key_x = element.x + 2;
        int val_x = element.x + 18;
        
        // Render Key
        for (size_t i = 0; i < line_pair.first.length(); ++i) {
            int c = key_x + i;
            if (c < element.x + element.width - 1) {
                buffer[line_y][c].character = line_pair.first[i];
                buffer[line_y][c].color = header_color;
            }
        }
        // Render Value
        for (size_t i = 0; i < line_pair.second.length(); ++i) {
            int c = val_x + i;
            if (c < element.x + element.width - 1) {
                buffer[line_y][c].character = line_pair.second[i];
                buffer[line_y][c].color = text_color;
            }
        }
        line_y++;
    }
}


std::string UI::formatVersion(uint32_t ver) {
    uint32_t major = (ver >> 22) & 0x3FF;
    uint32_t minor = (ver >> 12) & 0x3FF;
    uint32_t patch = (ver) & 0xFFF;
    std::stringstream ss;
    ss << major << "." << minor << "." << patch;
    return ss.str();
}

void UI::renderDebugStats(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element) {
    drawBox(buffer, element.x, element.y, element.width, element.height, "Debug Stats");

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << fps_display_value;
    std::string fps_str = "FPS: " + ss.str();
    ss.str(""); ss.clear();

    ss << current_player_pos.x << ", " << current_player_pos.y << ", " << current_player_pos.z;
    std::string pos_str = "Pos: " + ss.str();
    ss.str(""); ss.clear();

    ss << current_camera_dir.x << ", " << current_camera_dir.y << ", " << current_camera_dir.z;
    std::string dir_str = "Dir: " + ss.str();
    ss.str(""); ss.clear();

    ss << "Tris: " << triangle_count;
    std::string tri_str = ss.str();

    std::string mode_str = pixel_mode ? "Mode: Pixel" : "Mode: ASCII";
    std::vector<std::string> lines = {fps_str, pos_str, dir_str, tri_str, mode_str};
    RGBColor text_color = {200, 200, 200};

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_y = element.y + 1 + i;
        int line_x = element.x + 2;
        if (line_y < element.y + element.height - 1) {
            for (size_t j = 0; j < lines[i].length(); ++j) {
                if (line_x + j < element.x + element.width - 1) {
                    buffer[line_y][line_x + j].character = lines[i][j];
                    buffer[line_y][line_x + j].color = text_color;
                }
            }
        }
    }
}

void UI::renderMessageLog(std::vector<std::vector<ConsoleCell>>& buffer, const UIElement& element) {
    drawBox(buffer, element.x, element.y, element.width, element.height, "Message Log");

    int max_display_lines = element.height - 2;
    if (max_display_lines <= 0) return;
    
    size_t num_messages_to_display = std::min({message_log.size(), static_cast<size_t>(max_display_lines), MAX_MESSAGES});
    int current_line_y = element.y + 1;
    auto now = std::chrono::steady_clock::now();

    for (size_t i = 0; i < num_messages_to_display; ++i) {
        const auto& msg = message_log[message_log.size() - num_messages_to_display + i];
        
        std::chrono::duration<double> elapsed = now - msg.timestamp;
        long long elapsed_seconds = static_cast<long long>(elapsed.count());
        std::stringstream timestamp_ss;
        timestamp_ss << std::setw(3) << elapsed_seconds << "s";
        std::string timestamp_str = "[" + timestamp_ss.str() + "] ";

        std::string text_to_render = timestamp_str + msg.text;
        int available_width = element.width - 2;
        if (text_to_render.length() > static_cast<size_t>(available_width)) {
            text_to_render = text_to_render.substr(0, available_width);
        }

        int start_x = element.x + 1;
        for (size_t j = 0; j < text_to_render.length(); ++j) {
            buffer[current_line_y][start_x + j].character = text_to_render[j];
            buffer[current_line_y][start_x + j].color = msg.color;
        }
        current_line_y++;
    }
}

void UI::setGpuStaticStatus(bool enabled, bool shader_embedded, const std::string& device_name,
                            uint32_t vendor_id, uint32_t device_id, uint32_t api_version,
                            uint32_t driver_version, uint32_t queue_family) {
    vulkan_enabled = enabled;
    vulkan_shader_embedded = shader_embedded;
    vulkan_device_name = device_name;
    vulkan_vendor_id = vendor_id;
    vulkan_device_id = device_id;
    vulkan_api_version = api_version;
    vulkan_driver_version = driver_version;
    vulkan_queue_family = queue_family;
}

void UI::setGpuDynamicStatus(uint32_t out_w, uint32_t out_h, bool pipeline_ready, double last_ms) {
    vulkan_out_w = out_w;
    vulkan_out_h = out_h;
    vulkan_pipeline_ready = pipeline_ready;
    vulkan_last_dispatch_ms = last_ms;
}