{
    files = {
        [[src\console_utils.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "console_utils.o: src\\console_utils.cpp src\\console_utils.h src\\console.h  src\\vector.h src\\constants.h\
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