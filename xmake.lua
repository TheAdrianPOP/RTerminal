-- ─────────────────────────────────────────────────────────────────────────────
-- RTerminal build script
--
-- Windows/MinGW (auto-detected arch, or override with --arch=x86|x86_64):
--   xmake f --plat=windows --toolchain=mingw --sdk="C:/path/to/mingw32"
--   xmake f --plat=windows --arch=x86 --toolchain=mingw --sdk="C:/path/to/mingw32"  -- force 32-bit
--   xmake
--
-- Linux (auto-detected arch, or override with --arch=x86|x86_64):
--   xmake f --plat=linux
--   xmake f --plat=linux --arch=x86   -- force 32-bit
--   xmake
--
-- macOS:
--   xmake f --plat=macosx
--   xmake
-- ─────────────────────────────────────────────────────────────────────────────
--
-- NOTE: -flto is intentionally absent.
-- GCC 15.x / MinGW has a confirmed ICE in binds_to_current_def_p (symtab.cc).
-- ─────────────────────────────────────────────────────────────────────────────

option("analyze")
    set_default(false)
    set_showmenu(true)
    set_description("Print removed sections and write a linker map file")
option_end()

target("RTerminal")
    set_languages("c++17")

    add_files(
        "main.cpp",
        "src/game/**.cpp",
        "src/engine/**.cpp"
    )

    on_load(function(target)
        for _, dir in ipairs(os.dirs("src/**")) do
            target:add("includedirs", dir)
        end
    end)

    set_optimize("smallest")

    -- ── Release flags ─────────────────────────────────────────────────────────
    if is_mode("release") then

        -- Flags common to all platforms / compilers
        add_cxflags(
            "-Oz",
            "-fno-ident",
            "-ffast-math",
            "-fomit-frame-pointer",
            "-ffunction-sections",
            "-fdata-sections",
            "-fno-rtti",
            "-fno-exceptions",
            "-fno-stack-protector",
            "-fno-asynchronous-unwind-tables",
            "-fno-unwind-tables",
            "-fmerge-all-constants",
            {force = true}
        )

        add_defines("NDEBUG")

        -- x86 / x86_64: require SSE2 for Vec3 / SimdFloat4
        if is_arch("x86", "x86_64", "i386", "i686") then
            add_cxflags("-msse2", "-mfpmath=sse", {force = true})
        end

        -- ── Windows / MinGW specifics ─────────────────────────────────────────
        if is_plat("mingw", "windows") then

            -- i686 ABI only guarantees 4-byte stack alignment; SSE movaps needs 16.
            -- Only required (and valid) for 32-bit targets.
            if is_arch("x86", "i386", "i686") then
                add_cxflags("-mstackrealign", {force = true})
            end
            add_defines("NOMINMAX")

            add_ldflags(
                -- Drop libstdc++: new_delete.cpp provides what -nostdlib++ removes.
                "-nostdlib++",
                "-lwinmm",
                "-lxinput",
                "-lgdi32",
                -- Statically bake libgcc so no libgcc_s_*.dll is needed at runtime.
                "-static-libgcc",
                "-Wl,-Bdynamic",
                "-Wl,--gc-sections",
                "-Wl,-O1",
                "-Wl,--build-id=none",
                -- Wrap soft-float / libgcc helpers with hardware equivalents.
                "-Wl,--wrap,__powi",
                "-Wl,--wrap,__powisf2",
                "-Wl,--wrap,__extendsfdf2",
                "-Wl,--wrap,__adddf3",
                "-Wl,--wrap,__subdf3",
                "-Wl,--wrap,__muldf3",
                "-Wl,--wrap,__divdf3",
                "-Wl,--wrap,__floatsidf2",
                "-Wl,--wrap,__gttf2",
                "-Wl,--wrap,__unordf2",
                "-s",
                {force = true}
            )

            if has_config("analyze") then
                add_ldflags(
                    "-Wl,-Map," .. path.join(os.projectdir(), "build", "RTerminal.map"),
                    {force = true}
                )
                print("[analyze] Linker map will be written to build/RTerminal.map")
            end
        end

        -- ── Linux specifics ───────────────────────────────────────────────────
        if is_plat("linux") then
            add_ldflags(
                -- Fully static: zero external library dependencies.
                -- glibc, libstdc++, libgcc all baked in.
                -- LZMA compression in the packer recovers most of the size overhead.
                "-static",
                "-lpthread",
                "-Wl,--gc-sections",
                "-Wl,-O1",
                "-Wl,--build-id=none",
                "-s",
                {force = true}
            )

            if has_config("analyze") then
                add_ldflags(
                    "-Wl,-Map," .. path.join(os.projectdir(), "build", "RTerminal.map"),
                    {force = true}
                )
            end
        end

        -- ── macOS specifics ───────────────────────────────────────────────────
        if is_plat("macosx") then
            add_ldflags(
                "-Wl,-dead_strip",
                {force = true}
            )
        end

    end  -- is_mode("release")

    -- ── Post-build: pack + analyse (Windows only) ─────────────────────────────
    after_build(function (target)
        if is_mode("release") and is_plat("mingw", "windows", "linux") then
            import("lib.detect.find_tool")
            local python = find_tool("python3") or find_tool("python")

            local packer = path.join(os.projectdir(), "tools", "packer.py")
            if python and os.isfile(packer) then
                print("Packing executable with custom packer...")
                os.execv(python.program, {packer, target:targetfile()})
            else
                print("Warning: Packer not found or python missing.")
            end

            local analyzer = path.join(os.projectdir(), "tools", "map_analyzer.py")
            local mapfile  = path.join(os.projectdir(), "build", "RTerminal.map")
            if python and os.isfile(analyzer) and os.isfile(mapfile) then
                print("Analyzing binary size breakdown...")
                os.execv(python.program, {analyzer, mapfile})
            end
        end
    end)