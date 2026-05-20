#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "app_base.h"
#include "display.h"
#include "input.h"
#include "threading.h"
#include "shading_provider.h"
#include "skeleton.h"
#include "triangle.h"
#include "bvh.h"
#include "tlas.h"
#include "perf_timer.h"
#include "ui.h"
#include "vector.h"
#include "matrix.h"
#include "quat.h"
#include "str.h"
#include <cstdio>

// ── Global Metrics ───────────────────────────────────────────────────────────
GlobalMetrics g_metrics;

// ── Helper Utilities ─────────────────────────────────────────────────────────

struct NodeToJointMap {
    struct Entry { const cgltf_node* node; int joint_idx; };
    PodVector<Entry> entries;

    void insert(const cgltf_node* n, int i) { entries.push_back({n, i}); }
    int get(const cgltf_node* n) const {
        for (const auto& e : entries) if (e.node == n) return e.joint_idx;
        return -1;
    }
    bool contains(const cgltf_node* n) const { return get(n) != -1; }
};

// ── Animation & Model Data ───────────────────────────────────────────────────

struct JointTrack {
    int joint_index = -1;
    PodVector<float> t_times, s_times, r_times;
    PodVector<Vec3>  t_values;
    PodVector<float> s_values; // Uniform scale
    PodVector<Quat>  r_values;
};

struct AnimationClip {
    StrBuf<64> name;
    float      duration = 0.0f;
    PodVector<JointTrack> tracks;
};

class AnimatedModel {
public:
    Skeleton              rig;
    PodVector<Triangle>   bind_pose_mesh;
    PodVector<SkinVertex> skin_data;
    PodVector<Triangle>   deformed_mesh;
    BVH                   bvh;
    AnimationClip         animation;
    float                 current_time = 0.0f;

    bool loadGLB(const char* filepath) {
        cgltf_options options = {};
        cgltf_data* data = nullptr;
        if (cgltf_parse_file(&options, filepath, &data) != cgltf_result_success) return false;
        cgltf_load_buffers(&options, data, filepath);

        NodeToJointMap node_map;
        PodVector<Mat4> ibms;

        // 1. Parse Inverse Bind Matrices
        if (data->skins_count > 0 && data->skins[0].inverse_bind_matrices) {
            const cgltf_accessor* acc = data->skins[0].inverse_bind_matrices;
            ibms.resize(acc->count);
            for (cgltf_size i = 0; i < acc->count; ++i) {
                float v[16]; cgltf_accessor_read_float(acc, i, v, 16);
                for (int r=0; r<4; ++r) ibms[i].rows[r] = _mm_load_ps(&v[r*4]);
                ibms[i].transpose(); 
            }
        }

        // 2. Build Skeleton Hierarchy
        if (data->skins_count > 0) {
            const cgltf_skin& skin = data->skins[0];
            for (cgltf_size i = 0; i < skin.joints_count; ++i) {
                addJointRecursive(skin.joints[i], data, node_map, ibms);
            }
        }

        // 3. Parse Mesh Primitives
        PodVector<int> rig_indices;
        if (data->skins_count > 0) {
            for (cgltf_size i = 0; i < data->skins[0].joints_count; ++i)
                rig_indices.push_back(node_map.get(data->skins[0].joints[i]));
        }

        for (cgltf_size i = 0; i < data->meshes_count; ++i) {
            for (cgltf_size j = 0; j < data->meshes[i].primitives_count; ++j)
                loadPrimitive(data->meshes[i].primitives[j], rig_indices);
        }

        // 4. Finalize Rest Pose
        deformed_mesh.resize(bind_pose_mesh.size());
        rig.computeWorldTransforms();
        rig.skin(bind_pose_mesh, deformed_mesh, skin_data);
        bvh.source_triangles = &deformed_mesh;
        bvh.build(deformed_mesh);

        // 5. Parse Animation Tracks
        if (data->animations_count > 0) {
            const cgltf_animation& anim = data->animations[0];
            animation.name = anim.name ? anim.name : "Default";
            for (cgltf_size i = 0; i < anim.channels_count; ++i) {
                const auto& chan = anim.channels[i];
                if (!node_map.contains(chan.target_node)) continue;
                
                int j_idx = node_map.get(chan.target_node);
                JointTrack* track = nullptr;
                for (auto& t : animation.tracks) if (t.joint_index == j_idx) { track = &t; break; }
                if (!track) { animation.tracks.push_back({}); track = &animation.tracks.back(); track->joint_index = j_idx; }
                
                parseTrackData(*chan.sampler, chan.target_path, *track);
                animation.duration = fast_max(animation.duration, getTrackDuration(*track));
            }
        }

        cgltf_free(data);
        return true;
    }

    void update(float dt) {
        if (animation.duration <= 0.0f) return;
        current_time += dt;
        while (current_time >= animation.duration) current_time -= animation.duration;

        for (auto& track : animation.tracks) {
            auto& j = rig.joints[track.joint_index];
            samplePose(track, current_time, j.local_rotation, j.local_translation, j.local_scale);
        }

        rig.computeWorldTransforms();
        rig.skin(bind_pose_mesh, deformed_mesh, skin_data);
        bvh.refit();
    }

private:
    int addJointRecursive(cgltf_node* node, cgltf_data* data, NodeToJointMap& map, const PodVector<Mat4>& ibms) {
        if (map.contains(node)) return map.get(node);
        
        int p_idx = node->parent ? addJointRecursive(node->parent, data, map, ibms) : -1;
        
        const Mat4* ibm = nullptr;
        for (cgltf_size i = 0; i < data->skins[0].joints_count; ++i) {
            if (data->skins[0].joints[i] == node) { if (i < ibms.size()) ibm = &ibms[i]; break; }
        }

        Mat4 world_bind = getNodeWorldTransform(node);
        int idx = rig.addJoint(node->name ? node->name : "joint", p_idx, world_bind, ibm);
        map.insert(node, idx);
        return idx;
    }

    Mat4 getNodeWorldTransform(const cgltf_node* node) {
        Mat4 world = Mat4::identity();
        PodVector<const cgltf_node*> chain;
        for (const cgltf_node* n = node; n; n = n->parent) chain.push_back(n);

        for (int i = (int)chain.size() - 1; i >= 0; --i) {
            const auto* c = chain[i];
            Mat4 local;
            if (c->has_matrix) {
                float v[16]; for(int j=0; j<16; ++j) v[j] = c->matrix[j];
                for(int r=0; r<4; ++r) local.rows[r] = _mm_loadu_ps(&v[r*4]);
                local.transpose();
            } else {
                Vec3 p = c->has_translation ? Vec3(c->translation[0], c->translation[1], c->translation[2]) : Vec3(0,0,0);
                Quat r = c->has_rotation ? Quat(c->rotation[0], c->rotation[1], c->rotation[2], c->rotation[3]) : Quat::identity();
                float s = c->has_scale ? c->scale[0] : 1.0f;
                local = r.toMat4();
                for(int k=0; k<3; ++k) local.rows[k] = _mm_mul_ps(local.rows[k], _mm_set1_ps(s));
                alignas(16) float r0[4], r1[4], r2[4];
                _mm_store_ps(r0, local.rows[0]); _mm_store_ps(r1, local.rows[1]); _mm_store_ps(r2, local.rows[2]);
                r0[3] = p.x; r1[3] = p.y; r2[3] = p.z;
                local.rows[0] = _mm_load_ps(r0); local.rows[1] = _mm_load_ps(r1); local.rows[2] = _mm_load_ps(r2);
            }
            world = world * local;
        }
        return world;
    }

    void loadPrimitive(const cgltf_primitive& prim, const PodVector<int>& rig_indices) {
        cgltf_accessor *a_pos = nullptr, *a_norm = nullptr, *a_uv = nullptr, *a_j = nullptr, *a_w = nullptr;
        for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
            auto type = prim.attributes[i].type;
            if (type == cgltf_attribute_type_position) a_pos = prim.attributes[i].data;
            if (type == cgltf_attribute_type_normal)   a_norm = prim.attributes[i].data;
            if (type == cgltf_attribute_type_texcoord) a_uv = prim.attributes[i].data;
            if (type == cgltf_attribute_type_joints)   a_j = prim.attributes[i].data;
            if (type == cgltf_attribute_type_weights)  a_w = prim.attributes[i].data;
        }
        if (!a_pos || !prim.indices) return;

        for (cgltf_size i = 0; i < prim.indices->count; i += 3) {
            cgltf_uint idx[3] = { cgltf_accessor_read_index(prim.indices, i+0), cgltf_accessor_read_index(prim.indices, i+1), cgltf_accessor_read_index(prim.indices, i+2) };
            Vec3 p[3], n[3]; Vec2 uv[3];
            for (int v=0; v<3; ++v) {
                float tmp[3]; cgltf_accessor_read_float(a_pos, idx[v], tmp, 3); p[v] = {tmp[0], tmp[1], tmp[2]};
                if (a_norm) { cgltf_accessor_read_float(a_norm, idx[v], tmp, 3); n[v] = {tmp[0], tmp[1], tmp[2]}; }
                if (a_uv) { float t[2]; cgltf_accessor_read_float(a_uv, idx[v], t, 2); uv[v] = {t[0], t[1]}; }
            }
            Triangle tri(p[0], p[1], p[2], uv[0], uv[1], uv[2], 0);
            tri.n0 = n[0]; tri.n1 = n[1]; tri.n2 = n[2];
            bind_pose_mesh.push_back(tri);

            for (int v=0; v<3; ++v) {
                SkinVertex sv;
                if (a_j && a_w) {
                    cgltf_uint j[4]; float w[4];
                    cgltf_accessor_read_uint(a_j, idx[v], j, 4);
                    cgltf_accessor_read_float(a_w, idx[v], w, 4);
                    int cnt = 0;
                    for (int k=0; k<4; ++k) {
                        if (w[k] <= 0.0f) continue;
                        sv.bone_indices[cnt] = (j[k] < rig_indices.size()) ? rig_indices[j[k]] : 0;
                        sv.bone_weights[cnt] = w[k];
                        cnt++;
                    }
                    sv.count = fast_max(1, cnt);
                }
                skin_data.push_back(sv);
            }
        }
    }

    void parseTrackData(const cgltf_animation_sampler& sampler, cgltf_animation_path_type path, JointTrack& track) {
        auto read_times = [&](PodVector<float>& times) {
            times.resize(sampler.input->count);
            for (cgltf_size i=0; i<sampler.input->count; ++i) cgltf_accessor_read_float(sampler.input, i, &times[i], 1);
        };
        if (path == cgltf_animation_path_type_translation) {
            read_times(track.t_times); track.t_values.resize(sampler.output->count);
            for (cgltf_size i=0; i<sampler.output->count; ++i) { float v[3]; cgltf_accessor_read_float(sampler.output, i, v, 3); track.t_values[i] = {v[0],v[1],v[2]}; }
        } else if (path == cgltf_animation_path_type_rotation) {
            read_times(track.r_times); track.r_values.resize(sampler.output->count);
            for (cgltf_size i=0; i<sampler.output->count; ++i) { float q[4]; cgltf_accessor_read_float(sampler.output, i, q, 4); track.r_values[i] = {q[0],q[1],q[2],q[3]}; }
        } else if (path == cgltf_animation_path_type_scale) {
            read_times(track.s_times); track.s_values.resize(sampler.output->count);
            for (cgltf_size i=0; i<sampler.output->count; ++i) { float s[3]; cgltf_accessor_read_float(sampler.output, i, s, 3); track.s_values[i] = s[0]; }
        }
    }

    float getTrackDuration(const JointTrack& t) {
        float d = 0;
        if (!t.t_times.empty()) d = fast_max(d, t.t_times.back());
        if (!t.r_times.empty()) d = fast_max(d, t.r_times.back());
        if (!t.s_times.empty()) d = fast_max(d, t.s_times.back());
        return d;
    }

    void samplePose(const JointTrack& track, float time, Quat& out_r, Vec3& out_t, float& out_s) {
        auto sample_f = [&](const PodVector<float>& times, const auto& values, auto& out, auto lerp) {
            if (times.empty()) return;
            if (time <= times.front()) { out = values.front(); return; }
            if (time >= times.back())  { out = values.back(); return; }
            for (size_t i=0; i<times.size()-1; ++i) if (time >= times[i] && time < times[i+1]) {
                float t = (time - times[i]) / (times[i+1] - times[i]);
                out = lerp(values[i], values[i+1], t); return;
            }
        };
        sample_f(track.t_times, track.t_values, out_t, [](auto a, auto b, float t){ return a + (b-a)*t; });
        sample_f(track.r_times, track.r_values, out_r, [](auto a, auto b, float t){ return Quat::slerp(a, b, t); });
        sample_f(track.s_times, track.s_values, out_s, [](auto a, auto b, float t){ return a + (b-a)*t; });
    }
};

// ── Rendering & Shading ──────────────────────────────────────────────────────

class HandShader : public IShadingProvider {
public:
    const TLAS* tlas;
    ConsoleCell shadeHit(const Ray& ray, const HitRecord& hit, int x, int y) override {
        Vec3 light = Vec3(0.5f, 1.0f, -0.4f).normalize();
        float d = fast_max(0.1f, hit.shading_normal.dot(light));
        if (tlas->intersectsAny(Ray(hit.point + hit.shading_normal * 0.001f, light), 50.0f)) d *= 0.3f;
        
        if (hit.triangle->material_id == 1) {
            float bd = fast_max(0.4f, hit.shading_normal.dot(light));
            return ConsoleCell('+', {(int)(50*bd), (int)(255*bd), (int)(50*bd)}, false, true);
        }
        return ConsoleCell(' ', {(int)(220*d), (int)(180*d), (int)(160*d)}, false, false);
    }
    ConsoleCell shadeMiss(const Ray&) override { return ConsoleCell(' ', {15, 20, 30}, false, false); }
};

class HandUI : public UI {
public:
    int num_joints = 0; float anim_time = 0;
    void render(PodVector<PodVector<ConsoleCell>>& b, int w, int h) override {
        ctx.buffer = &b; ctx.screen_w = w; ctx.screen_h = h;
        Begin("ENGINE STATS", 2, 1, 28, 10, Colors::BORDER);
        char buf[32];
        snprintf(buf, 32, "%.1f ms", frame_time_ms); DrawKeyValue("Frame", buf, Colors::VALUE);
        snprintf(buf, 32, "%.0f", fps_display_value); DrawKeyValue("FPS", buf, Colors::OK);
        snprintf(buf, 32, "%d", num_joints); DrawKeyValue("Bones Found", buf, Colors::TITLE);
        snprintf(buf, 32, "%.2fs", anim_time); DrawKeyValue("Anim Time", buf, Colors::STATUS_KEY);
        Separator();
        TextColored(Colors::INACTIVE, " [M]   Toggle HUD");
        TextColored(Colors::INACTIVE, " [B]   Toggle Bones");
        TextColored(Colors::INACTIVE, " [ESC] Quit");
        End();
        const char* hnt = "LMB: Rotate   MMB: Pan   SCROLL: Zoom";
        Begin(nullptr, (w-(int)strlen(hnt)-4)/2, h-3, (int)strlen(hnt)+4, 3, {40, 45, 60});
        TextColored(Colors::STATUS_KEY, " %s", hnt); End();
    }
};

// ── Application Entry ────────────────────────────────────────────────────────

class HandApp : public AppBase {
    Display display_; InputManager input_; ParallelExecutor executor_;
    TLAS    tlas_, bone_tlas_;
    AnimatedModel hand_; HandShader shader_; HandUI ui_; GraphicsConfig gfx_;
    PodVector<Triangle> bone_tris_; BVH bone_bvh_;
    PodVector<PodVector<ConsoleCell>> ui_buffer_;

    float cam_yaw_ = -45.0f, cam_pitch_ = 20.0f, cam_radius_ = 10.0f;
    Vec3  cam_target_ = {0,0,0};
    bool  ui_visible_ = true, show_bones_ = true;

public:
    HandApp() : AppBase(60.0f), executor_(4) {}

    bool onInit() override {
        gfx_.pixel_mode = true; gfx_.color_mode = ColorMode::TrueColor;
        ui_.bindInput(&input_);
        if (!hand_.loadGLB("hand.glb")) return false;
        tlas_.addInstance(&hand_.bvh, Mat4::identity(), Mat4::identity());
        tlas_.build();
        updateBones();
        bone_tlas_.addInstance(&bone_bvh_, Mat4::identity(), Mat4::identity());
        bone_tlas_.build();
        shader_.tlas = &tlas_;
        return true;
    }

    void updateBones() {
        bone_tris_.clear();
        if (show_bones_) {
            for (const auto& j : hand_.rig.joints) if (j.parent_index >= 0) {
                Vec3 p = hand_.rig.joints[j.parent_index].world_transform.transformPoint({0,0,0});
                Vec3 c = j.world_transform.transformPoint({0,0,0});
                addBone(p, c, 0.05f, 1);
            }
        }
        if (bone_tris_.empty()) addBone({0,0,0}, {0,0.01f,0}, 0.01f, 1);
        bone_bvh_.build(bone_tris_);
    }

    void addBone(const Vec3& from, const Vec3& to, float w, uint32_t mid) {
        Vec3 d = to - from; float l = d.magnitude(); if (l < 0.01f) return; d = d / l;
        Vec3 u = (fast_abs(d.y) != 1.0f) ? Vec3(0,1,0) : Vec3(1,0,0);
        Vec3 r = u.cross(d).normalize(); u = d.cross(r).normalize();
        float s = l * 0.2f;
        Vec3 b = from, t = to, m1 = b+d*s+r*w, m2 = b+d*s-r*w, m3 = b+d*s+u*w, m4 = b+d*s-u*w;
        auto tri = [&](auto v1, auto v2, auto v3) { bone_tris_.push_back(Triangle(v1, v2, v3, {0,0},{1,0},{0,1}, mid)); };
        tri(b, m1, m3); tri(b, m3, m2); tri(b, m2, m4); tri(b, m4, m1);
        tri(t, m3, m1); tri(t, m2, m3); tri(t, m4, m2); tri(t, m1, m4);
    }

    void onFrame(float dt) override {
        input_.update();
        if (input_.isKeyPressed(KEY_ESC)) exit(0);
        if (input_.isKeyPressed(KEY_M)) ui_visible_ = !ui_visible_;
        if (input_.isKeyPressed(KEY_B)) show_bones_ = !show_bones_;
        
        float w = input_.getMouseWheelDelta(); if (w != 0.0f) cam_radius_ = fast_clamp(cam_radius_-w*2.0f, 2.0f, 1000.0f);
        if (input_.isMouseButtonDown(MOUSE_LEFT) || input_.isMouseButtonDown(MOUSE_RIGHT)) {
            Vec2 d = input_.getMouseDelta(); cam_yaw_ += d.u; cam_pitch_ = fast_clamp(cam_pitch_+d.v, -89.0f, 89.0f);
        }
        if (input_.isMouseButtonDown(MOUSE_MIDDLE)) {
            Vec2 d = input_.getMouseDelta(); float s = cam_radius_*0.002f, y = degToRad(cam_yaw_);
            cam_target_ = cam_target_ + Vec3(fast_cos(y),0,-fast_sin(y))*(-d.u*s) + Vec3(0,1,0)*(d.v*s);
        }

        hand_.update(dt);
        updateBones();
        tlas_.refit(); bone_tlas_.refit();

        ui_.num_joints = (int)hand_.rig.joints.size(); ui_.anim_time = hand_.current_time;
        ui_.setRenderTime(dt*1000.0f); ui_.update(dt, cam_target_, Vec3(), current_fps, Vec3(), false, ui_visible_);

        float p = degToRad(cam_pitch_), y = degToRad(cam_yaw_);
        Vec3 eye(cam_target_.x+cam_radius_*fast_cos(p)*fast_sin(y), cam_target_.y+cam_radius_*fast_sin(p), cam_target_.z+cam_radius_*fast_cos(p)*fast_cos(y));
        Vec3 f = (cam_target_-eye).normalize(), r = Vec3(0,1,0).cross(f).normalize(), u = f.cross(r).normalize();
        float fs = fast_tan(degToRad(gfx_.fov)*0.5f), asp = (width/2.0f)/height;
        
        display_.Resize(width, height); ConsoleCell* buf = display_.GetBuffer();
        executor_.run(height, [&](int py) {
            float ny = 1.0f-(2.0f*(py+0.5f)/height);
            for (int px=0; px<width; ++px) {
                float nx = (2.0f*(px+0.5f)/width)-1.0f;
                Ray ray(eye, (f + r*(nx*asp*fs) + u*(ny*fs)).normalize());
                TLASHit bh = bone_tlas_.intersect(ray, gfx_.render_dist);
                if (bh.hit()) { 
                    HitRecord hr; hr.point=eye+ray.direction*bh.t; hr.shading_normal=bh.normal; hr.triangle=bh.triangle; 
                    buf[py*width+px] = shader_.shadeHit(ray, hr, px, py); continue; 
                }
                TLASHit h = tlas_.intersect(ray, gfx_.render_dist);
                if (h.hit()) { 
                    HitRecord hr; hr.point=eye+ray.direction*h.t; hr.shading_normal=h.normal; hr.triangle=h.triangle; 
                    buf[py*width+px] = shader_.shadeHit(ray, hr, px, py); 
                } else buf[py*width+px] = shader_.shadeMiss(ray);
            }
        });
        
        if (ui_visible_) {
            if ((int)ui_buffer_.size() != height) { ui_buffer_.resize(height); for (auto& row : ui_buffer_) row.resize(width); }
            for (auto& row : ui_buffer_) for (auto& cell : row) cell = ConsoleCell();
            ui_.render(ui_buffer_, width, height);
            for (int py=0; py<height; ++py) for (int px=0; px<width; ++px) if (ui_buffer_[py][px].is_ui) buf[py*width+px] = ui_buffer_[py][px];
        }
        display_.Flush(width, height, gfx_);
    }
};

int main() { HandApp app; app.run(); return 0; }