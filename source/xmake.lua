-- Lazy-100 runtime kernel: a static library every host/example links against.
target("lazy100-static")
    set_kind("static")
    set_languages("cxx23")
    add_files("lazy100/**.cpp")
    add_includedirs(os.scriptdir(), {public = true})
    add_headerfiles("lazy100/**.hpp")
    add_packages("libsdl3", "miniaudio", "lua", "sol2", "vri", "stb", {public = true})
target_end()
