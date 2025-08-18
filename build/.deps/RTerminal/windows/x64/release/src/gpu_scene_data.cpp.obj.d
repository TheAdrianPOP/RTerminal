{
    depfiles = "gpu_scene_data.o: src\\gpu_scene_data.cpp src\\gpu_scene_data.h  src\\vector.h src\\constants.h src\\material.h src\\texture.h src\\ui.h  src\\console.h src\\light.h src\\triangle.h src\\object.h src\\ray.h  src\\aabb.h src\\bvh.h\
",
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
    files = {
        [[src\gpu_scene_data.cpp]]
    },
    depfiles_format = "gcc"
}