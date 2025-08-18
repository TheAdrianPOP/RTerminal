#ifndef NOISE_H
#define NOISE_H

#include <vector>
#include <numeric>
#include <random>
#include <algorithm>
#include <cmath>
#include "vector.h" // Assuming Vec3 is defined here

// Helper functions for Perlin noise
namespace Perlin {

    inline double lerp(double a, double b, double t) {
        return a + t * (b - a);
    }

    inline double fade(double t) {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }

    inline double grad(int hash, double x, double y, double z) {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    class PerlinNoise {
    public:
        PerlinNoise() {
            p.resize(512);
            std::iota(p.begin(), p.begin() + 256, 0);
            
            // Use a fixed seed for reproducible noise, or a time-based seed for variation
            unsigned int seed = 42; // Fixed seed for now
            std::default_random_engine engine(seed);
            std::shuffle(p.begin(), p.begin() + 256, engine);

            for (int i = 0; i < 256; ++i) {
                p[i + 256] = p[i];
            }
        }

        double noise(double x, double y, double z) const {
            int floorX = static_cast<int>(std::floor(x));
            int floorY = static_cast<int>(std::floor(y));
            int floorZ = static_cast<int>(std::floor(z));

            int X = floorX & 255;
            int Y = floorY & 255;
            int Z = floorZ & 255;

            x -= floorX;
            y -= floorY;
            z -= floorZ;

            double u = fade(x);
            double v = fade(y);
            double w = fade(z);

            int A = p[X] + Y;
            int AA = p[A] + Z;
            int AB = p[A + 1] + Z;
            int B = p[X + 1] + Y;
            int BA = p[B] + Z;
            int BB = p[B + 1] + Z;

            return lerp(lerp(lerp(grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z), u),
                           lerp(grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z), u), v),
                       lerp(lerp(grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1), u),
                           lerp(grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1), u), v), w);
        }
        
        // Add a noise function that takes a Vec3
        double noise(const Vec3& p) const {
            return noise(p.x, p.y, p.z);
        }

    private:
        std::vector<int> p;
    };

    // Provide a global instance or a way to get one
    inline PerlinNoise& getPerlinNoise() {
        static PerlinNoise instance;
        return instance;
    }

} // namespace Perlin

#endif // NOISE_H 