#ifndef BVH_H
#define BVH_H

#include "aabb.h"
#include "object.h" // Needs full definition for vector<const Object*>
#include "ray.h"
#include "constants.h"
#include <vector>
#include <memory>
#include <limits>
#include <algorithm> // For std::min/max during build
#include <numeric> // For std::iota
#include <functional> // For std::function

struct BVHNode {
    AABB bounds;
    std::unique_ptr<BVHNode> left = nullptr;
    std::unique_ptr<BVHNode> right = nullptr;
    std::vector<const Object*> objects; // Pointers to objects in this leaf node

    bool isLeaf() const { return !left && !right; }
};

// --- Binning Data Structure ---
struct Bin {
    AABB bounds;
    int object_count = 0;
};

class BVH {
public:
    std::unique_ptr<BVHNode> root = nullptr;

    // --- Modified buildRecursive using Binning ---
    std::unique_ptr<BVHNode> buildRecursive(std::vector<const Object*> current_objects, int depth) { // Pass vector by value or modify in place carefully
        if (current_objects.empty()) return nullptr;

        auto node = std::make_unique<BVHNode>();

        // Calculate bounds for all objects in this node
        for (const auto* obj : current_objects) {
            if (obj) node->bounds.expand(obj->getAABB());
        }
        // Handle degenerate case where bounds have zero volume (e.g., flat plane)
        if (node->bounds.surfaceArea() < EPSILON) {
            // Treat as leaf if bounds are degenerate
            node->objects = std::move(current_objects); // Move objects into the node
            return node;
        }


        // --- Leaf Node Check ---
        if (current_objects.size() <= MAX_OBJECTS_PER_LEAF || depth >= MAX_BVH_DEPTH) {
            node->objects = std::move(current_objects); // Move objects into the node
            return node;
        }

        // --- Binning Based Split ---
        Vec3 centroid_bounds_min(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());
        Vec3 centroid_bounds_max(-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity());
        for (const auto* obj : current_objects) {
            if (!obj) continue;
            Vec3 centroid = obj->getAABB().center();
            centroid_bounds_min.x = std::min(centroid_bounds_min.x, centroid.x);
            centroid_bounds_min.y = std::min(centroid_bounds_min.y, centroid.y);
            centroid_bounds_min.z = std::min(centroid_bounds_min.z, centroid.z);
            centroid_bounds_max.x = std::max(centroid_bounds_max.x, centroid.x);
            centroid_bounds_max.y = std::max(centroid_bounds_max.y, centroid.y);
            centroid_bounds_max.z = std::max(centroid_bounds_max.z, centroid.z);
        }
        Vec3 extent = centroid_bounds_max - centroid_bounds_min;

        int best_axis = -1;
        double best_split_coord = 0.0;
        double min_sah_cost = std::numeric_limits<double>::infinity();
        const double parent_surface_area = node->bounds.surfaceArea();
        const double traversal_cost = 0.125; // Heuristic cost of traversing an internal node

        // Iterate through potential split axes (0=x, 1=y, 2=z)
        for (int axis = 0; axis < 3; ++axis) {
            double axis_extent = (axis == 0) ? extent.x : ((axis == 1) ? extent.y : extent.z);
            if (axis_extent < EPSILON) continue;

            const int num_bins = 8; // Or 16, 32. Power of 2 often convenient.
            std::vector<Bin> bins(num_bins);
            double bin_size = axis_extent / num_bins;
            double inverse_bin_size = (bin_size > EPSILON) ? 1.0 / bin_size : 0.0; // Avoid division by zero
            double axis_min_coord = (axis == 0) ? centroid_bounds_min.x : ((axis == 1) ? centroid_bounds_min.y : centroid_bounds_min.z);


            // Populate bins
            for (const auto* obj : current_objects) {
                if (!obj) continue;
                Vec3 centroid = obj->getAABB().center();
                double centroid_coord = (axis == 0) ? centroid.x : ((axis == 1) ? centroid.y : centroid.z);

                // Calculate bin index, clamping to [0, num_bins - 1]
                int bin_idx = static_cast<int>((centroid_coord - axis_min_coord) * inverse_bin_size);
                bin_idx = std::clamp(bin_idx, 0, num_bins - 1);

                bins[bin_idx].object_count++;
                bins[bin_idx].bounds.expand(obj->getAABB());
            }

            // Evaluate potential splits between bins
            for (int i = 0; i < num_bins - 1; ++i) {
                AABB left_bounds, right_bounds;
                int left_count = 0, right_count = 0;

                // Accumulate stats for left side (bins 0 to i)
                for (int j = 0; j <= i; ++j) {
                    left_bounds.expand(bins[j].bounds);
                    left_count += bins[j].object_count;
                }

                // Accumulate stats for right side (bins i+1 to num_bins-1)
                for (int j = i + 1; j < num_bins; ++j) {
                    right_bounds.expand(bins[j].bounds);
                    right_count += bins[j].object_count;
                }

                // Skip invalid splits (empty sides)
                if (left_count == 0 || right_count == 0) continue;

                // Calculate SAH cost for this potential split plane
                double left_area = left_bounds.surfaceArea();
                double right_area = right_bounds.surfaceArea();
                double prob_left = (parent_surface_area > EPSILON) ? left_area / parent_surface_area : 0.0;
                double prob_right = (parent_surface_area > EPSILON) ? right_area / parent_surface_area : 0.0;
                double cost = traversal_cost + prob_left * left_count + prob_right * right_count;


                // Update best split if this one is better
                if (cost < min_sah_cost) {
                    min_sah_cost = cost;
                    best_axis = axis;
                    // Split coordinate is the boundary between bin i and bin i+1
                    best_split_coord = axis_min_coord + (i + 1) * bin_size;
                }
            } // End loop evaluating splits for current axis
        } // End loop over axes

        // --- Fallback or Leaf Creation ---
        // Cost heuristic: If the best split cost is higher than just making this a leaf, make it a leaf.
        // Cost of leaf = number of objects * intersection_cost (assume intersection_cost = 1.0 for simplicity)
        double leaf_cost = static_cast<double>(current_objects.size());

        if (best_axis == -1 || min_sah_cost >= leaf_cost) {
            // No good split found, or making it a leaf is cheaper.
            node->objects = std::move(current_objects);
            return node;
        }

        // --- Partition Objects Based on Best Split ---
        std::vector<const Object*> left_objects;
        std::vector<const Object*> right_objects;
        left_objects.reserve(current_objects.size()); // Reserve full size for potential worst case
        right_objects.reserve(current_objects.size());

        for (const auto* obj : current_objects) {
            if (!obj) continue;
            Vec3 centroid = obj->getAABB().center();
            double centroid_coord = (best_axis == 0) ? centroid.x : ((best_axis == 1) ? centroid.y : centroid.z);
            if (centroid_coord < best_split_coord) {
                left_objects.push_back(obj);
            } else {
                right_objects.push_back(obj);
            }
        }

        // --- Handle Empty Partition (Rare with good binning, but possible) ---
        if (left_objects.empty() || right_objects.empty()) {
             // If binning resulted in an empty partition (e.g., all objects at the exact same coordinate),
             // fall back to a simple median split to ensure progress.
             size_t mid_point = current_objects.size() / 2;
             if (mid_point == 0) { // Safety check
                 node->objects = std::move(current_objects);
                 return node;
             }
             // Use std::partition or similar for potential efficiency, but simple split is ok
             // Create temporary vectors for assignment if buildRecursive needs them moved later
             std::vector<const Object*> temp_left(current_objects.begin(), current_objects.begin() + mid_point);
             std::vector<const Object*> temp_right(current_objects.begin() + mid_point, current_objects.end());
             left_objects = std::move(temp_left); // Move into the vectors we'll use
             right_objects = std::move(temp_right);

              // Final safety check if even the median split failed (e.g., only 1 object left somehow)
              if (left_objects.empty() || right_objects.empty()) {
                   node->objects = std::move(current_objects); // Move remaining objects into leaf
                   return node;
              }
        }

        // --- Recurse ---
        // Pass the partitioned object lists to the recursive calls.
        // Note: `buildRecursive` now takes `vector` by value or needs careful modification in place.
        // Passing by value is simpler here. `std::move` can be used when passing to avoid copies
        // if the original `current_objects` is no longer needed in this scope.
        node->left = buildRecursive(std::move(left_objects), depth + 1);
        node->right = buildRecursive(std::move(right_objects), depth + 1);

        // Original current_objects vector is now empty or irrelevant after moves

        return node;
    }

    // --- Build Entry Point ---
    void build(const std::vector<std::unique_ptr<Object>>& scene_objects) {
        // Create a vector of const pointers for the build process
        std::vector<const Object*> build_ptrs;
        build_ptrs.reserve(scene_objects.size());
        for (const auto& obj_ptr : scene_objects) {
            if (obj_ptr) build_ptrs.push_back(obj_ptr.get());
        }

        // Start recursive build - Pass the pointer vector by value
        root = build_ptrs.empty() ? nullptr : buildRecursive(std::move(build_ptrs), 0);
        // build_ptrs is now empty after move
    }

    bool intersect(const Ray& ray, double& t_hit, const Object*& hit_object, Vec3& hit_normal, double& hit_u, double& hit_v) const {
        if (!root) return false;

        // Initial check with root AABB using the new intersect method
        if (root->bounds.intersect(ray, EPSILON, std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity()) {
            return false; // Ray doesn't even hit the root node's bounding box
        }

        t_hit = std::numeric_limits<double>::infinity(); // Initialize closest hit distance
        hit_object = nullptr;
        bool found_hit = false;

        // Use std::vector as stack (or switch to fixed array if preferred and depth is bounded)
        std::vector<const BVHNode*> node_stack;
        node_stack.reserve(64); // Reserve space to potentially avoid reallocations
        node_stack.push_back(root.get());


        while (!node_stack.empty()) {
            const BVHNode* current_node = node_stack.back();
            node_stack.pop_back();

            // We already know the ray hits this node's AABB at some point (from parent or root check).
            // We must check if it hits *closer* than the current closest object hit 't_hit'.
            // The AABB::intersect call now handles the t_max check internally.
            // We just need to ensure the returned distance is less than t_hit.
            // Note: AABB::intersect returns std::max(t_min, t_enter_slab).
            // If the intersection t is >= t_hit, we can skip this node.
            if (current_node->bounds.intersect(ray, EPSILON, t_hit) >= t_hit) {
                 continue;
            }


            if (current_node->isLeaf()) {
                // Leaf node: intersect with objects
                for (const Object* obj : current_node->objects) {
                    if (!obj) continue;
                    double t; Vec3 n; double u, v;
                    // Call the object's specific intersection test
                    if (obj->intersect(ray, t, n, u, v)) {
                        // Check if this hit is closer than the current closest hit
                        // and also greater than a small epsilon to avoid self-intersection artifacts.
                        if (t < t_hit && t > EPSILON) {
                            t_hit = t; // Update closest hit distance
                            hit_object = obj;
                            hit_normal = n;
                            hit_u = u;
                            hit_v = v;
                            found_hit = true;
                            // The updated t_hit will help prune subsequent AABB checks
                        }
                    }
                }
            } else {
                // Internal node: Check children and push them onto the stack,
                // prioritizing the one whose AABB is hit closer by the ray.

                const BVHNode* left_child = current_node->left.get();
                const BVHNode* right_child = current_node->right.get();

                // Calculate intersection distances for children using the current t_hit as the upper bound
                double t_left = (left_child) ? left_child->bounds.intersect(ray, EPSILON, t_hit) : std::numeric_limits<double>::infinity();
                double t_right = (right_child) ? right_child->bounds.intersect(ray, EPSILON, t_hit) : std::numeric_limits<double>::infinity();

                // Check if the intersections are valid (i.e., less than infinity, which also implies < t_hit)
                bool left_hit = t_left < std::numeric_limits<double>::infinity();
                bool right_hit = t_right < std::numeric_limits<double>::infinity();

                // Decide push order based on intersection distance
                if (left_hit && right_hit) {
                    // Both children's AABBs are hit closer than t_hit.
                    // Push the node whose AABB is *farther* away first,
                    // so that the node with the closer AABB is processed next.
                    if (t_left < t_right) {
                        node_stack.push_back(right_child);
                        node_stack.push_back(left_child); // Left is closer, process next
                    } else {
                        node_stack.push_back(left_child);
                        node_stack.push_back(right_child); // Right is closer (or equal), process next
                    }
                } else if (left_hit) {
                    // Only left child's AABB is hit closer than t_hit
                    node_stack.push_back(left_child);
                } else if (right_hit) {
                    // Only right child's AABB is hit closer than t_hit
                    node_stack.push_back(right_child);
                }
                // If neither child's AABB is hit closer than t_hit, do nothing (branch pruned)
            }
        } // End while stack not empty

        return found_hit;
    }

    bool intersectsAny(const Ray& ray, double max_t) const {
        if (!root) return false;

        // Use the new AABB::intersect, checking against max_t
        if (root->bounds.intersect(ray, EPSILON, max_t) == std::numeric_limits<double>::infinity()) {
             return false;
        }

        std::vector<const BVHNode*> node_stack;
        node_stack.reserve(64);
        node_stack.push_back(root.get());

        while (!node_stack.empty()) {
            const BVHNode* current_node = node_stack.back();
            node_stack.pop_back();

            // Check if node is hit within the allowed range
            if (current_node && current_node->bounds.intersect(ray, EPSILON, max_t) < max_t) {
                if (current_node->isLeaf()) {
                    for (const Object* obj : current_node->objects) {
                        if (!obj) continue;
                        double t; Vec3 n; double u, v; // n, u, v aren't used but needed by interface
                        if (obj->intersect(ray, t, n, u, v) && t > EPSILON && t < max_t) {
                            return true; // Found any hit within range
                        }
                    }
                } else {
                    // No need to prioritize order for intersectsAny, just push valid children
                    if (current_node->left && current_node->left->bounds.intersect(ray, EPSILON, max_t) < max_t) {
                        node_stack.push_back(current_node->left.get());
                    }
                    if (current_node->right && current_node->right->bounds.intersect(ray, EPSILON, max_t) < max_t) {
                        node_stack.push_back(current_node->right.get());
                    }
                }
            }
        }
        return false; // No intersection found within max_t
    }

    void queryOverlap(const AABB& query_box, std::vector<const Object*>& overlapping_objects) const {
         if (!root || !root->bounds.overlaps(query_box)) return;
         std::vector<const BVHNode*> node_stack;
         node_stack.reserve(64);
         node_stack.push_back(root.get());
         while (!node_stack.empty()) {
             const BVHNode* current_node = node_stack.back();
             node_stack.pop_back();
             // Check overlap before processing node
             if (current_node && current_node->bounds.overlaps(query_box)) {
                 if (current_node->isLeaf()) {
                     for (const Object* obj : current_node->objects) {
                         // Check individual object overlap as well
                         if (obj && obj->getAABB().overlaps(query_box)) {
                             overlapping_objects.push_back(obj);
                         }
                     }
                 } else {
                     // Push children if they exist (overlap check happens when they are popped)
                     if (current_node->left) node_stack.push_back(current_node->left.get());
                     if (current_node->right) node_stack.push_back(current_node->right.get());
                 }
             }
         }
     }
};

#endif // BVH_H 