#ifndef MATERIAL_H
#define MATERIAL_H

#include "vector.h"
#include "texture.h" // Include Texture header
#include <string>
#include <memory> // For std::shared_ptr

class Material {
public:
    std::string name;
    char symbol;
    Vec3 color;
    double ambient;
    double diffuse;
    double specular;
    double shininess;
    std::shared_ptr<Texture> texture; // Pointer to texture

    // New members for bump mapping
    bool use_auto_bump = false;
    double auto_bump_strength = 1.0; // How strong the bump effect is

    // Constructor
    Material(char s, const Vec3& col, double amb, double diff, double spec, double shin,
             std::shared_ptr<Texture> tex = nullptr, bool auto_bump = false, double bump_strength = 1.0)
        : symbol(s), color(col), ambient(amb), diffuse(diff), specular(spec), shininess(shin),
          texture(tex), use_auto_bump(auto_bump), auto_bump_strength(bump_strength) {}

    // Default constructor
    Material() : symbol('?'), color(0.5, 0.5, 0.5), ambient(0.1), diffuse(0.8), specular(0.0), shininess(1.0),
                 texture(nullptr), use_auto_bump(false), auto_bump_strength(1.0) {}

    // Other methods?
};

#endif // MATERIAL_H 