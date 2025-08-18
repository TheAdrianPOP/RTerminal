{
    files = {
        [[src\player.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "player.o: src\\player.cpp src\\player.h src\\vector.h src\\constants.h  src\\ray.h src\\aabb.h src\\object.h src\\material.h src\\texture.h src\\ui.h  src\\console.h src\\scene.h src\\triangle.h src\\light.h src\\bvh.h  src\\collision_utils.h src\\input.h\
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