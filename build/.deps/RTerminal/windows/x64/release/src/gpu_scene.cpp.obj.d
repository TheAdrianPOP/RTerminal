{
    depfiles_format = "gcc",
    files = {
        [[src\gpu_scene.cpp]]
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
    depfiles = "gpu_scene.o: src\\gpu_scene.cpp src\\gpu_scene.h src\\vector.h  src\\constants.h src\\scene.h src\\triangle.h src\\object.h src\\ray.h  src\\aabb.h src\\material.h src\\texture.h src\\ui.h src\\console.h  src\\light.h src\\bvh.h\
"
}