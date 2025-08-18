{
    files = {
        [[src\ui.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "ui.o: src\\ui.cpp src\\ui.h src\\console.h src\\vector.h src\\constants.h\
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