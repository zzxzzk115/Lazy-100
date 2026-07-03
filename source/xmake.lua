-- Lazy-100 runtime kernel: a static library every host/example links against.
target("lazy100-static")
    set_kind("static")
    set_languages("cxx23")
    add_files("lazy100/**.cpp")
    add_includedirs(os.scriptdir(), {public = true})
    add_headerfiles("lazy100/**.hpp")
    add_packages("libsdl3", "miniaudio", "lua", "sol2", "vri", "stb", "vfilesystem", "nlohmann_json",
                 {public = true})
    add_deps("z8lua", {public = true}) -- the p8-dialect Lua fork (P8Vm; native carts use lua+sol2)
    if not is_plat("wasm") then
        add_packages("libcurl", {public = true}) -- net::Fetch desktop backend
    end
target_end()
