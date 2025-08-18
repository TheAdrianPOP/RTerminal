#ifndef TEXTURE_H
#define TEXTURE_H

#include <string>
#include <vector>
#include <memory> // For std::shared_ptr
#include <map>    // For static cache
#include "vector.h" // For Vec2, Vec3
#include "constants.h" // For ANSI_RESET
#include "ui.h" // Include UI header

// Represents a single cell in the texture data
struct TextureData {
    // Store only the 256-color ANSI ID
    int color_id = 15; // Default to white (ANSI color 15)
};

class Texture {
public:
    // Static factory method to load/retrieve textures (handles caching)
    static std::shared_ptr<Texture> load(const std::string& filename, UI& ui_layer);

    // Get texture data for given UV coordinates
    // Returns true if texture provided specific data, false otherwise.
    // Note: Character will now be a default or handled by the caller.
    bool getCharacterAndColor(double u, double v, char& out_char, std::string& out_ansi_code, Vec3& out_base_color) const;

    // Public getter for dimensions (if needed elsewhere)
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // Consider adding: Vec3 getVec3ColorAt(double u, double v) const; for direct color sampling

private:
    // Private constructor - use Texture::load() instead
    Texture() : width(0), height(0) {}

    // Private loading function used by the static load method
    bool loadFromFileInternal(const std::string& filename, UI& ui_layer);

    int width;
    int height;
    std::vector<std::vector<TextureData>> data;

    // Static cache for loaded textures
    static std::map<std::string, std::shared_ptr<Texture>> loaded_textures_cache;
};

#endif // TEXTURE_H 