# RTerminal

A from-scratch C++ framework for building terminal-native 3D applications. Provides a full Whitted ray tracer, physically-based shading, BVH/TLAS acceleration, ECS, SPH fluid simulation, 3D audio, and more — with zero STL or third-party runtime dependencies.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)
![License](https://img.shields.io/badge/license-Apache%202.0-green)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)

---

## Overview

RTerminal is a self-contained framework designed for applications that render inside a terminal emulator. It handles everything from ANSI output and input to ray tracing, physics, and audio — letting you focus on your application rather than platform plumbing.

The codebase deliberately avoids the STL and has no external runtime dependencies. Every component — dynamic arrays, strings, smart pointers, atomics, threading — is implemented from scratch and designed to work together.

---

## Features

**Rendering**
- Whitted ray tracer with recursive reflections, refractions, and Schlick fresnel
- Fast ray caster fallback for performance-constrained terminals
- 4-wide QBVH acceleration structure with SAH binned builder and SSE2 triangle packing
- Two-level TLAS for instanced geometry
- Ambient occlusion (RTAO), Reinhard and ACES tone mapping
- Bayer 8×8 ordered dithering for 256-color and 16-color terminals
- TrueColor (24-bit), ANSI 256, ANSI 16, and monochrome output modes
- Procedural sky and cloud rendering with Perlin noise
- Dirty-region tracking to minimize terminal output bandwidth
- 2D drawing layer (`DrawCtx`) with lines, rects, circles, and text

**Core Framework**
- Entity-Component System with sparse sets and arena allocation
- Cross-platform affinity-pinned worker thread pool
- Lock-free SPSC ring buffer
- FNV-1a compile-time (`SID`) and runtime string IDs
- Localization string table (CSV / INI)
- Event bus with synchronous and deferred dispatch
- Tween system with 20+ easing curves
- Callback system with small-buffer optimization (no heap for closures ≤ 24 bytes)
- Timer system (one-shot and repeating)
- Animation state machine with condition-driven transitions
- Reference-counted asset manager with hot-reload support
- `Result<T, E>` error type
- Binary reader / writer for serialization

**Physics & Simulation**
- Capsule and multi-sphere collision shapes with fixed-timestep resolution
- SPH fluid simulation with spatial hashing neighbor search
- Eulerian 3D fluid grid (advection + diffusion)
- Particle system with emitter, RNG, and camera-facing billboard renderer

**Math**
- SSE2-accelerated `Vec3`, `Mat4`, `Quat`
- `SimdFloat4` and `SimdFloat8` (AVX2 path with SSE2 fallback)
- `Vec2`, `Vec2i`, `AABB`, `Ray`, `Rect`, `Recti`
- Bezier (quadratic + cubic), Catmull-Rom splines, arc-length parameterization
- Keyframe float curve, `Color` with HSV conversion and alpha blend
- Full easing library (quad, cubic, quartic, sine, expo, elastic, back, bounce)

**Audio**
- PCM mixer with up to 64 simultaneous voices
- 3D spatialization with distance attenuation and stereo panning
- WAV loading with automatic resampling to 44100 Hz stereo
- DSP chain: low-pass, high-pass, reverb (Schroeder), distortion, gain
- Streaming audio callback

**Platform**
- Windows: Win32 console, XInput gamepad, ANSI VT processing
- Linux: POSIX terminal raw mode, SGR extended mouse tracking, ALSA audio
- macOS: POSIX terminal
- Custom `operator new/delete`, no libstdc++ dependency on Windows (MinGW)
- `AppBase` frame loop with fixed FPS cap, resize detection, and SIGINT handling
- LZMA-compressed self-extracting binary packer (Windows in-memory PE loader + Linux `memfd`)

---

## Getting Started

Derive from `AppBase` and implement `onInit()` and `onFrame()`:

```cpp
#include "app_base.h"
#include "display.h"
#include "ray_tracer.h"

class MyApp : public AppBase {
    Display display;
    BVH     bvh;

protected:
    bool onInit() override {
        // build your scene, load assets
        return true;
    }

    void onFrame(float dt) override {
        display.Resize(width, height);
        // trace rays, write to display.GetBuffer()
        display.Flush(width, height, gfx_config);
    }
};

int main() {
    runtime_init();
    MyApp app;
    app.run();
}
```

---

## Building

**Requirements**
- [xmake](https://xmake.io) build system
- C++17 compiler: GCC 11+, Clang 13+, or MinGW 12+
- SSE2-capable x86 / x86_64 CPU

**Linux**
```bash
xmake f --plat=linux
xmake
```

**Windows (MinGW)**
```bash
xmake f --plat=windows --toolchain=mingw --sdk="C:/path/to/mingw64"
xmake
```

**macOS**
```bash
xmake f --plat=macosx
xmake
```

**Force 32-bit**
```bash
xmake f --arch=x86 ...
```

**Size analysis**
```bash
xmake f --analyze=true && xmake
```

---

## Third-Party Notices

- **LZMA micro-decoder stubs** (`stub/`) by Ilya Kurdyukov — [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/)
- **miniz** (`src/engine/`) — public domain
- **cynes nes emulator** (modified) — [MIT License](https://opensource.org/licenses/MIT)

All other code is original and covered by the Apache 2.0 license below.

---

## License

Copyright 2024 RTerminal Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.