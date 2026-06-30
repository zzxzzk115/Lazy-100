-- The Lazy-100 host executable: `xmake run lazy100 [cart.lua]`.
target("lazy100")
    set_kind("binary")
    set_languages("cxx23")
    add_files("run/main.cpp")
    add_deps("lazy100-static")
    -- VRI's Vulkan backend calls the loader directly; the vri package compiles against
    -- headers only, so link vulkan-1 from the system Vulkan SDK into the final executable.
    if is_plat("windows") then
        local vk = os.getenv("VULKAN_SDK")
        if vk then
            add_linkdirs(path.join(vk, "Lib"))
            add_links("vulkan-1")
        end
    end
target_end()
