-- The Lazy-100 host executable: `xmake run lazy100 [cart.lua]`.
target("lazy100")
    set_kind("binary")
    set_languages("cxx23")
    add_files("run/main.cpp")
    add_deps("lazy100-static")

    -- `xmake run` defaults the working dir to the exe's folder; use the project root so
    -- relative cart paths (carts/, examples/carts/) resolve for ls/load/save.
    set_rundir("$(projectdir)")

    -- Link the built-in assets (font, ...) into the executable so it is self-contained:
    -- Windows via an .rc RCDATA resource, Unix via an .incbin assembly object.
    if is_plat("windows") then
        add_files("$(projectdir)/embed/lazy100.rc")
    elseif is_plat("linux") or is_plat("macosx") then
        add_files("$(projectdir)/embed/lazy100.S")
    end

    -- GL-only build: no Vulkan loader is linked. (When the VRI Vulkan backend is enabled the
    -- host must link vulkan-1 from the system SDK - but do it by the loader's full path, never
    -- by adding $VULKAN_SDK/Lib as a search dir: that dir also ships MD-built spirv-cross libs
    -- that would shadow our MT spirv-cross and break the link with LNK2038.)
target_end()
