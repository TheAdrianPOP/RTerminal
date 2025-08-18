{
    files = {
        [[src\shading.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "shading.o: src\\shading.cpp src\\shading.h src\\vector.h src\\constants.h  src\\material.h src\\texture.h src\\ui.h src\\console.h src\\scene.h  src\\triangle.h src\\object.h src\\ray.h src\\aabb.h src\\light.h src\\bvh.h  src\\skybox.h\
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
    }
}