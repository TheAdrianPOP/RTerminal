{
    files = {
        [[src\skybox.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "skybox.o: src\\skybox.cpp src\\skybox.h src\\vector.h src\\constants.h  src\\ray.h src\\scene.h src\\triangle.h src\\object.h src\\aabb.h  src\\material.h src\\texture.h src\\ui.h src\\console.h src\\light.h  src\\bvh.h src\\noise.h src\\shading.h\
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