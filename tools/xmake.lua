-- xmake.lua for spv2h tool
target("spv2h")
    set_kind("binary")
    set_languages("c++17")
    add_files("spv2h.cpp")
    set_targetdir(".")
