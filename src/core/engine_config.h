#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include "internal.h"

constexpr int kAudioSampleRate     = 44100;
constexpr int kAudioChannels       = 2;
constexpr int kAudioBytesPerSample = 2;
constexpr int kAudioMaxVoices      = 64;
constexpr int kAudioMixFrames      = 512;
constexpr int kAudioNumBuffers     = 4;
constexpr float kAudioMaxDist        = 50.f;
constexpr float kAudioRefDist        = 1.0f;
constexpr int kAudioBufBytes       = kAudioMixFrames * kAudioChannels * kAudioBytesPerSample;

constexpr double kMsPerSecond = 1000.0;
constexpr double kNsToMs      = 1000000.0;
constexpr double kMsToSeconds = 0.001;
constexpr float kDefaultTargetFps  = 60.f;
constexpr float kDefaultMaxDtScale = 5.f;

constexpr int kIntStrMaxBytes     = 12;
constexpr int kAnsi16BrightOffset = 8;
constexpr int kAnsi16FgBase       = 30;
constexpr int kAnsi16BrightFgBase = 90;
constexpr int kAnsi16BgBase       = 40;
constexpr int kAnsi16BrightBgBase = 100;
constexpr int kAnsi16Count        = 16;
constexpr int kAnsiMaxCode      = 255;
constexpr int kAnsi16SystemEnd  = 16;
constexpr int kAnsiCubeEnd      = 231;
constexpr int kAnsiGrayBase     = 232;
constexpr int kAnsiGraySteps    = 24;
constexpr int kAnsiGrayRange    = 247;
constexpr int kAnsiGrayInputMin = 8;
constexpr int kAnsiGrayInputMax = 248;
constexpr int kAnsiCubeStride_R = 36;
constexpr int kAnsiCubeStride_G = 6;
constexpr float kAnsiInitMinDist  = 1e18f;
constexpr float kAnsiGrayStep     = 10.f;
constexpr float kAnsiGrayStartF   = 8.f;

constexpr uint32_t kEntityIndexBits      = 20u;
constexpr uint32_t kEntityIndexMask      = (1u << kEntityIndexBits) - 1u;
constexpr uint32_t kEntityGenShift       = kEntityIndexBits;
constexpr uint32_t kEntityGenMask        = 0xFFFu;
constexpr uint32_t kEntityNullValue      = 0xFFFF'FFFFu;
constexpr size_t kDefaultEcsArenaInitialBytes = 1ull * 1024ull * 1024ull;

constexpr int kTriPackLanes      = 4;
constexpr int kBvhStackDepth     = 64;
constexpr int kBvhStackSafeLimit = kBvhStackDepth - 2;
constexpr int kBvhSampleMask     = 0xFF;
constexpr float kBvhMetricsScale   = 256.0f;

constexpr int kTlasMaxLeafInstances = 2;
constexpr int kTlasStackDepth       = 64;
constexpr int kTlasStackSafeLimit   = kTlasStackDepth - 2;
constexpr int MAX_BONE_INFLUENCES = 4;
constexpr int MAX_JOINTS          = 256;
constexpr int kNoParent           = -1;

constexpr float kOneThird = 0.333333f;
constexpr float kBayerRange            = 64.0f;
constexpr int kRampSlotCount         = 64;
constexpr float kRec601_R              = 0.299f;
constexpr float kRec601_G              = 0.587f;
constexpr float kRec601_B              = 0.114f;
constexpr float kBayerDitherSaturation = 3.f;
constexpr float kBayerDitherSpread     = 55.f;
constexpr float kBayerDitherMidpoint   = 0.2f;
constexpr int kPercR                 = 3;
constexpr int kPercG                 = 6;
constexpr int kPercB                 = 1;
constexpr float kLumaBand1             = 51.f;
constexpr float kLumaBand2             = 102.f;
constexpr float kLumaBand3             = 153.f;
constexpr float kLumaBand4             = 204.f;

constexpr int kTexDefaultPixel = kAnsiMaxCode;
constexpr int kTexMsgBufSize   = 64;

constexpr size_t kBinWriterInitialCap   = 256;
constexpr size_t kBinWriterGrowthFactor = 2;
constexpr size_t kBinWriterMinCap       = 1;
constexpr size_t kBinWriterMaxStrBytes  = 65535u;

constexpr int MIN_CONSOLE_WIDTH          = 80;
constexpr int MIN_CONSOLE_HEIGHT         = 25;
constexpr int UI_STATUS_BAR_HEIGHT       = 3;
constexpr const char* ANSI_RESET         = "\x1b[0m";
constexpr size_t CONSOLE_BUFFER_SIZE        = 1024u * 1024u;
constexpr size_t DISPLAY_OUTPUT_INITIAL_SIZE = 1024u * 1024u;
constexpr size_t DISPLAY_WRITE_CHUNK_SIZE   = 65536u;
constexpr size_t DISPLAY_SAFETY_MARGIN      = 2048u;

constexpr int MAX_OBJECTS_PER_LEAF = 8;
constexpr int MAX_BVH_DEPTH        = 32;
constexpr float BVH_TRAVERSAL_COST   = 0.125f;
constexpr int BVH_NUM_BINS         = 16;

constexpr int DEFAULT_THREAD_COUNT_FALLBACK = 4;
constexpr int kMaxSpheresPerShape   = 8;
constexpr float kParticleNearClip     = 0.01f;
constexpr float kParticleUpThresh     = 0.9f;
constexpr uint32_t kParticleRngDefaultSeed = 12345u;
constexpr float kFluidDepthSentinel   = 1e30f;
constexpr float kFluidRampIndexScale  = 4.99f;
constexpr float kFluidGrid3DNeighbors = 6.f;
constexpr float kFluidAdvectCellLo    = 0.5f;
constexpr float kFluidAdvectCellHi    = 1.5f;
constexpr float kParticleDefaultSpread   = 0.5f;
constexpr float kParticleDefaultSpeedMin = 1.f;
constexpr float kParticleDefaultSpeedMax = 3.f;
constexpr float kParticleDefaultLifeMin  = 0.5f;
constexpr float kParticleDefaultLifeMax  = 2.f;
constexpr float kParticleDefaultRate     = 20.f;
constexpr float kCapsuleDefaultRadius     = 0.25f;
constexpr float kCapsuleDefaultHalfHeight = 0.65f;

constexpr float kSPHDefaultH            = 0.2f;
constexpr float kSPHDefaultRestDensity  = 1000.f;
constexpr float kSPHDefaultStiffness    = 200.f;
constexpr float kSPHDefaultViscosity    = 0.5f;
constexpr float kSPHDefaultParticleMass = 0.02f;
constexpr float kSPHDefaultBoundStiff   = 300.f;

constexpr float kFluidGridRenderThreshold = 0.05f;
#include "str.h"
#include "vector.h"

const Vec3 kParticleDefaultDirection = Vec3(0.f, 1.f, 0.f);
const Vec3 kParticleDefaultGravity   = Vec3(0.f, -4.f, 0.f);
const RGBColor kParticleDefaultColor = {255, 200, 80};
const Vec3 kSPHDefaultBoundMin       = Vec3(-1.f, 0.f, -1.f);
const Vec3 kSPHDefaultBoundMax       = Vec3( 1.f, 2.f,  1.f);

enum class ColorMode {
    TrueColor,
    Ansi256,
    Ansi16,
    Ansi8,
    Monochrome
};

struct SystemConfig {
    int    thread_count        = 0;
    size_t console_buffer_size = CONSOLE_BUFFER_SIZE;
};

struct GraphicsConfig {
    float fov            = 70.0f;
    float shadow_brightness = 0.4f;
    float ambient_light  = 0.3f;
    float diffuse_strength = 0.7f;
    float shadow_bias    = 0.01f;
    float render_dist    = 1000.0f;
    bool use_raytracer = true;

    bool  enable_ao      = true;
    float ao_radius      = 0.6f;
    float ao_strength    = 0.35f;

    float ambient_bounce     = 0.5f;
    float exposure           = 0.0f;
    float gamma              = 2.2f;
    float tone_mapping_peak  = 1.0f;

    float bump_strength  = 1.0f;
    Vec3  luma_weights   = Vec3(0.2126f, 0.7152f, 0.0722f);

    StrBuf<256> ascii_ramp {
        "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. " };

    float visual_noise_char_min = 33.0f;
    float visual_noise_char_max = 126.0f;
    float visual_noise_gray_min = 50.0f;
    float visual_noise_gray_max = 200.0f;

    float smoothing_threshold = 45.0f;

    bool show_normals = false;
    bool pixel_mode   = false;
    bool show_ui      = true;

    ColorMode color_mode = ColorMode::TrueColor;
};

#endif