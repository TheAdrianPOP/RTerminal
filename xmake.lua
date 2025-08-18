-- xmake.lua for RTerminal - Vulkan headless renderer

-- Define the target executable name
target("RTerminal")
    -- Use C++17 for modern features (std::clamp, std::optional, etc.)
    set_languages("c++20")
    add_defines("USE_VULKAN")

    -- Add source files
    add_files("main.cpp", "src/*.cpp")

    -- Add Vulkan SDK paths
    add_includedirs("E:/VulkanSDK/Include")
    add_linkdirs("E:/VulkanSDK/Lib")

    -- Global optimization settings (default is faster builds)
    set_optimize("smallest")

    -- Platform-specific Vulkan linking
    if is_plat("windows") then
        add_links("vulkan-1")
    elseif is_plat("linux") then
        add_links("vulkan")
    elseif is_plat("macosx") then
        add_links("vulkan")
        add_frameworks("Metal", "QuartzCore")
    end

-- Utility: build a small tool to convert SPIR-V to header for embedding
target("spv2h")
    set_kind("binary")
    set_languages("c++17")
    add_files("tools/spv2h.cpp")

    -- GCC/Clang optimizations (MinGW or Linux/macOS)
    if is_plat("mingw") or is_plat("linux", "macosx", "windows") then
        if is_mode("release") then
            -- Speed-optimized release build
            add_cxflags("-O3")
            add_cxflags("-ffast-math", "-ftree-vectorize", "-funroll-loops")
            add_cxflags("-fomit-frame-pointer", "-ffunction-sections", "-fdata-sections")
            add_cxflags("-march=native")

            add_ldflags("-Wl,--gc-sections")
            add_ldflags("-s")
            add_ldflags("-flto") -- Also add -flto to linker flags for consistency
        end
    end

