{
    files = {
        [[tools\spv2h.cpp]]
    },
    depfiles_format = "gcc",
    depfiles = "spv2h.o: tools\\spv2h.cpp\
",
    values = {
        [[C:\msys64\mingw64\bin\x86_64-w64-mingw32-g++]],
        {
            "-std=c++17",
            "-fexceptions",
            "-O3",
            "-ffast-math",
            "-ftree-vectorize",
            "-funroll-loops",
            "-fomit-frame-pointer",
            "-ffunction-sections",
            "-fdata-sections",
            "-march=native"
        }
    }
}