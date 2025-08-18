#ifndef VECTOR_H
#define VECTOR_H

#include <cmath>
#include "constants.h" // For EPSILON

struct Vec2 {
    double u, v;
    Vec2() : u(0.0), v(0.0) {}
    Vec2(double u, double v) : u(u), v(v) {}
};

struct Vec3 {
    double x, y, z;
    Vec3() : x(0.0), y(0.0), z(0.0) {}
    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
    Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
    Vec3 operator*(double scalar) const { return Vec3(x * scalar, y * scalar, z * scalar); }
    Vec3 operator/(double scalar) const {
        if (scalar * scalar < EPSILON * EPSILON) return Vec3(); // Avoid division by zero or near-zero
        return Vec3(x / scalar, y / scalar, z / scalar);
    }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    double dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }
    Vec3 cross(const Vec3& other) const {
        return Vec3(y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x);
    }
    double magnitudeSq() const { return x * x + y * y + z * z; } // Squared magnitude
    double magnitude() const { return std::sqrt(magnitudeSq()); }
    Vec3 normalize() const {
        double mag = magnitude();
        if (mag * mag > EPSILON * EPSILON) { return *this / mag; }
        return *this; // Return original vector if magnitude is too small
    }
};

#endif // VECTOR_H 