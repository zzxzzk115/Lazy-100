-- The Lazy-100 host executable: `xmake run lazy100 [cart.lua]`.
target("lazy100")
    set_kind("binary")
    set_languages("cxx23")
    add_files("run/main.cpp")
    add_deps("lazy100-static")

    -- Link the built-in assets (font, ...) into the executable so it is self-contained:
    -- Windows via an .rc RCDATA resource, Unix via an .incbin assembly object.
    if is_plat("windows") then
        add_files("$(projectdir)/embed/lazy100.rc")
    elseif is_plat("linux") or is_plat("macosx") then
        add_files("$(projectdir)/embed/lazy100.S")
    end

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
