{
    files = {
        [[src\texture.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "texture.o: src\\texture.cpp src\\texture.h src\\vector.h src\\constants.h  src\\ui.h src\\console.h\
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