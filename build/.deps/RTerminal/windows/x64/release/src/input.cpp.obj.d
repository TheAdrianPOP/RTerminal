{
    files = {
        [[src\input.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "input.o: src\\input.cpp src\\input.h src\\vector.h src\\constants.h\
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