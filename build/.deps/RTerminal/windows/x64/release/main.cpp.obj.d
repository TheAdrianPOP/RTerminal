{
    files = {
        "main.cpp"
    },
    values = {
        [[C:\msys64\mingw64\bin\x86_64-w64-mingw32-g++]],
        {
            "-Os",
            "-std=c++20",
            [[-IE:\VulkanSDK\Include]],
            "-DUSE_VULKAN",
            "-fexceptions"
        }
    },
    depfiles_format = "gcc",
    depfiles = "main.o: main.cpp src/constants.h src/vector.h src/constants.h src/ray.h  src/vector.h src/aabb.h src/ray.h src/texture.h src/ui.h src/console.h  src/material.h src/texture.h src/object.h src/aabb.h src/material.h  src/triangle.h src/object.h src/light.h src/bvh.h src/scene.h  src/triangle.h src/light.h src/bvh.h src/console.h src/collision_utils.h  src/player.h src/scene.h src/collision_utils.h src/input.h src/shading.h  src/skybox.h src/noise.h src/skybox.h src/console_utils.h src/renderer.h  src/console_utils.h src/input.h\
"
}