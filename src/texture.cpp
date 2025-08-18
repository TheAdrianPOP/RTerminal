#include "texture.h"
#include <fstream>
#include <iostream> // For placeholder std::cerr
#include "ui.h" // Include UI header
#include <sstream> // Required for stringstream parsing
#include <limits> // Required for std::numeric_limits
#include <algorithm> // Required for std::max, std::min
#include <cmath> // Required for std::fmod, std::floor

// Helper function to convert ANSI 256 color ID to Vec3 (RGB, 0.0-1.0)
// Placed inside an anonymous namespace or as a static private member if preferred
namespace { // Anonymous namespace for helper
    Vec3 ansi256_to_rgb(int ansi_code) {
        if (ansi_code < 0 || ansi_code > 255) {
            return Vec3(1.0, 1.0, 1.0); // Default to white for invalid codes
        }

        // Standard and High-Intensity colors (0-15)
        if (ansi_code < 16) {
            // These are approximations and might need tweaking for exact console appearance
            switch (ansi_code) {
                case 0:  return Vec3(0, 0, 0);        // Black
                case 1:  return Vec3(0.5, 0, 0);      // Red (Dark Red)
                case 2:  return Vec3(0, 0.5, 0);      // Green (Dark Green)
                case 3:  return Vec3(0.5, 0.5, 0);    // Yellow (Dark Yellow/Brown)
                case 4:  return Vec3(0, 0, 0.5);      // Blue (Dark Blue)
                case 5:  return Vec3(0.5, 0, 0.5);    // Magenta (Dark Magenta)
                case 6:  return Vec3(0, 0.5, 0.5);    // Cyan (Dark Cyan)
                case 7:  return Vec3(0.75, 0.75, 0.75);// White (Light Gray)
                case 8:  return Vec3(0.5, 0.5, 0.5);  // Bright Black (Dark Gray)
                case 9:  return Vec3(1, 0, 0);        // Bright Red
                case 10: return Vec3(0, 1, 0);        // Bright Green
                case 11: return Vec3(1, 1, 0);        // Bright Yellow
                case 12: return Vec3(0, 0, 1);        // Bright Blue
                case 13: return Vec3(1, 0, 1);        // Bright Magenta
                case 14: return Vec3(0, 1, 1);        // Bright Cyan
                case 15: return Vec3(1, 1, 1);        // Bright White
            }
        }

        // 6x6x6 Color Cube (16-231)
        if (ansi_code >= 16 && ansi_code <= 231) {
            int code = ansi_code - 16;
            int r_val = code / 36;
            int g_val = (code % 36) / 6;
            int b_val = code % 6;

            // Convert 0-5 range to 0.0-1.0 (approximate)
            // Values are 0, 95, 135, 175, 215, 255 in 8-bit RGB
            auto map_val = [](int v) {
                if (v == 0) return 0.0;
                return (static_cast<double>(v) * 40.0 + 55.0) / 255.0;
            };
            // Simpler mapping for 0-5 to 1 range: v / 5.0
            // Let's use a slightly more standard mapping based on typical terminal levels
            auto map_channel = [](int val) {
                 // Levels: 0, 95, 135, 175, 215, 255 (0 to 5)
                if (val == 0) return 0.0;
                if (val == 1) return 95.0 / 255.0;
                if (val == 2) return 135.0 / 255.0;
                if (val == 3) return 175.0 / 255.0;
                if (val == 4) return 215.0 / 255.0;
                if (val == 5) return 255.0 / 255.0;
                return 0.0; // Should not happen
            };
            return Vec3(map_channel(r_val), map_channel(g_val), map_channel(b_val));
        }

        // Grayscale Ramp (232-255)
        if (ansi_code >= 232 && ansi_code <= 255) {
            int gray_level = ansi_code - 232;
            // Approximate grayscale value (10 levels from black to white, not quite linear)
            // Levels are roughly 8, 18, 28, ... up to 248
            double gray = (static_cast<double>(gray_level) * 10.0 + 8.0) / 255.0;
            gray = std::max(0.0, std::min(gray, 1.0)); // Ensure it's in range using min/max instead of clamp
            return Vec3(gray, gray, gray);
        }
        return Vec3(1.0,1.0,1.0); // Should not be reached if input is 0-255
    }

    // Catmull-Rom spline interpolation for a single Vec3 value.
    // Takes four control points (p0, p1, p2, p3) and an interpolation parameter t (0 to 1).
    // p1 and p2 are the points being interpolated between, p0 and p3 are context.
    // This specific form is the "cardinal" spline with tension 0 (standard Catmull-Rom).
    /*
    Vec3 catmull_rom_interpolate(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t) {
        double t2 = t * t;
        double t3 = t2 * t;

        // Coefficients for the cubic polynomial
        double c0 = -0.5 * t3 + 1.0 * t2 - 0.5 * t;
        double c1 = 1.5 * t3 - 2.5 * t2 + 1.0;
        double c2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
        double c3 = 0.5 * t3 - 0.5 * t2;

        // Weighted sum of control points
        return p0 * c0 + p1 * c1 + p2 * c2 + p3 * c3;
    }
    */

    // Helper to get texel data safely for sampling.
    // Clamps coordinates to valid texture bounds [0, width-1], [0, height-1].
    TextureData get_safe_texel(const std::vector<std::vector<TextureData>>& data, int width, int height, int x, int y) {
        // Clamp coordinates to the actual texture dimensions.
        // Using std::max/min with 0 and width/height-1 ensures valid indices.
        int clamped_x = std::max(0, std::min(x, width - 1));
        int clamped_y = std::max(0, std::min(y, height - 1));

        // Return the data from the clamped position.
        return data[clamped_y][clamped_x];
    }
} // end anonymous namespace

// Initialize static cache member
std::map<std::string, std::shared_ptr<Texture>> Texture::loaded_textures_cache;

// Modified to accept ui_layer
std::shared_ptr<Texture> Texture::load(const std::string& filename, UI& ui_layer) {
    auto it = loaded_textures_cache.find(filename);
    if (it != loaded_textures_cache.end()) {
        return it->second;
    }
    std::shared_ptr<Texture> texture(new Texture());
    // Pass ui_layer to loadFromFileInternal
    if (texture && texture->loadFromFileInternal(filename, ui_layer)) {
        loaded_textures_cache[filename] = texture;
        return texture;
    }
    return nullptr;
}

// Modified to accept ui_layer and parse color ID only
bool Texture::loadFromFileInternal(const std::string& filename, UI& ui_layer) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        ui_layer.addMessage("TEXTURE LOAD ERROR: Could not open file: " + filename, RGBColor{255, 50, 50});
        return false;
    }

    data.clear();
    std::string line_str;
    int max_parsed_texels_in_any_row = 0;

    ui_layer.addMessage("TEXTURE LOAD INFO: Loading '" + filename + "' (Color ID format)", RGBColor{150, 150, 255});

    int line_number = 0;
    while (std::getline(file, line_str)) {
        line_number++;
        // Trim leading/trailing whitespace
        line_str.erase(0, line_str.find_first_not_of(" \t\n\r\f\v"));
        line_str.erase(line_str.find_last_not_of(" \t\n\r\f\v") + 1);

        if (line_str.empty() || line_str[0] == '#') continue; // Skip empty lines and comments

        std::stringstream ss(line_str);
        std::vector<TextureData> row_data;
        int color_id;
        int texels_in_this_row = 0;

        // Parse color IDs
        while (ss >> color_id) {
            TextureData tex_data;
            if (color_id >= 0 && color_id <= 255) {
                tex_data.color_id = color_id;
            } else {
                 ui_layer.addMessage("TEXTURE PARSE WARNING: Invalid color ID " + std::to_string(color_id) + " in " + filename + " line " + std::to_string(line_number) + ". Defaulting to white (15).", RGBColor{255, 150, 50});
                tex_data.color_id = 15; // Default to white
            }

            row_data.push_back(tex_data);
            texels_in_this_row++;

            // Check for potential parsing errors if the format is not just integers
             if (ss.fail() && !ss.eof()) {
                 ui_layer.addMessage("TEXTURE PARSE WARNING: Format error parsing line " + std::to_string(line_number) + " in " + filename + ". Expected space-separated integers.", RGBColor{255, 150, 50});
                 ss.clear(); // Clear error flags
                 ss.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Discard rest of the line
                 break; // Stop parsing this line
             }
        }

        if (!row_data.empty()) {
            data.push_back(row_data);
            if (texels_in_this_row > max_parsed_texels_in_any_row) {
                max_parsed_texels_in_any_row = texels_in_this_row;
            }
        } else {
            // If a line was not empty but no data was parsed, it might be a format error
             if (!line_str.empty() && line_str[0] != '#') {
                 ui_layer.addMessage("TEXTURE PARSE WARNING: No valid data (integers) parsed from line " + std::to_string(line_number) + " in " + filename + ". Check format (space-separated integers).", RGBColor{255, 150, 50});
             }
        }
    }
    file.close();

    this->width = max_parsed_texels_in_any_row;
    this->height = static_cast<int>(data.size()); // height is number of successfully parsed rows

    ui_layer.addMessage("TEXTURE LOAD INFO: Finished '" + filename + "'. Determined dims: " + std::to_string(this->width) + "x" + std::to_string(this->height), RGBColor{150, 150, 255});

    if (this->width == 0 || this->height == 0) {
        ui_layer.addMessage("TEXTURE LOAD WARNING: Loaded empty texture (0 width or height) from " + filename, RGBColor{255, 150, 50});
        return false;
    }

    // Ensure all rows have the same width by padding with default data
    for (size_t i = 0; i < data.size(); ++i) {
        while (static_cast<int>(data[i].size()) < this->width) {
            data[i].push_back(TextureData()); // Add default (color_id 15)
        }
        if (static_cast<int>(data[i].size()) > this->width) {
             // This case means some rows had more valid IDs than the max determined. Truncate.
            ui_layer.addMessage("TEXTURE LOAD WARNING: Row " + std::to_string(i+1) + " in " + filename + " has more texels (" + std::to_string(data[i].size()) + ") than determined width (" + std::to_string(this->width) + "). Truncating.", RGBColor{255, 150, 50});
            data[i].resize(this->width);
        }
    }
    return true;
}

// Modified to sample color ID and return default character
bool Texture::getCharacterAndColor(double u, double v, char& out_char, std::string& out_ansi_code, Vec3& out_base_color) const {
    if (width == 0 || height == 0 || data.empty() || data[0].empty()) {
        out_char = ' '; // Default to space or another indicator
        out_ansi_code = ANSI_RESET;
        out_base_color = Vec3(0.5, 0.5, 0.5); // Default base color (grey)
        return false;
    }

    // Clamp UV coordinates to [0, 1)
    u = std::fmod(u, 1.0); if (u < 0.0) u += 1.0;
    v = std::fmod(v, 1.0); if (v < 0.0) v += 1.0;

    // Calculate floating-point pixel coordinates (0 to width/height).
    // py is flipped because texture V typically increases downwards.
    double px = u * width;
    double py = (1.0 - v) * height;

    // --- Get Color ID (using Nearest Neighbor sampling) ---
    int nearest_x = static_cast<int>(std::floor(px + 0.5));
    int nearest_y = static_cast<int>(std::floor(py + 0.5));

    // Clamp nearest coordinates to ensure they are within bounds
    nearest_x = std::max(0, std::min(nearest_x, width - 1));
    nearest_y = std::max(0, std::min(nearest_y, height - 1));

    if (nearest_y >= 0 && nearest_y < static_cast<int>(data.size()) &&
        nearest_x >= 0 && nearest_x < static_cast<int>(data[nearest_y].size())) {
        const auto& nearest_tex_data = data[nearest_y][nearest_x];

        // Character is no longer stored in texture data, return a default or handle elsewhere
        out_char = ' '; // Using space as a default character

        out_base_color = ansi256_to_rgb(nearest_tex_data.color_id); // Convert ID to Vec3
        out_ansi_code = "\x1b[38;5;" + std::to_string(nearest_tex_data.color_id) + "m"; // Construct ANSI string
    } else {
        // Fallback if out of bounds somehow (shouldn't happen with clamping).
        out_char = ' '; // Default character on error
        out_ansi_code = ANSI_RESET; // Reset color
        out_base_color = Vec3(0.5, 0.0, 0.5); // Magenta-ish for error
    }
    // --- End Nearest Neighbor Sampling ---

    // Always return true if we have valid dimensions, as clamping ensures valid lookup.
    return true;
}