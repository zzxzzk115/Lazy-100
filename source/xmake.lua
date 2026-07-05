-- Lazy-100 runtime kernel: a static library the host app and tools link against.
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
        if is_plat("linux") and is_arch("armv7") then
            -- armv7 cross: the multiarch system curl, linked directly (headers are the
            -- arch-independent /usr/include/curl; see external/xmake.lua for the why).
            add_syslinks("curl", {public = true})
        else
            add_packages("libcurl", {public = true}) -- net::Fetch desktop backend
        end
    end
    if is_plat("linux") then
        -- VRI's GL backend opens its context through EGL (+ the wayland-egl window bridge)
        -- but the vri package doesn't declare either lib; link them for every binary here.
        add_syslinks("EGL", "wayland-egl", {public = true})
    end
target_end()

-- The host application: the `lazy100` executable that links the kernel above.
includes("lazy100-app")
