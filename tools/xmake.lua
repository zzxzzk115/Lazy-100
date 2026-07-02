-- Host tools. cartshot: headless first-frame screenshot of a cart -> PNG preview
-- (for the Lazy-100-games catalog; no window/GPU, so it runs in CI too).
target("cartshot")
    set_kind("binary")
    set_languages("cxx23")
    set_default(false) -- built on demand: xmake build cartshot
    add_files("cartshot/main.cpp")
    add_deps("lazy100-static")
    set_rundir("$(projectdir)")
    -- Embed the built-in assets (font) exactly like the host binary, so print() renders.
    if is_plat("windows") then
        add_files("$(projectdir)/embed/lazy100.rc")
    elseif is_plat("linux") or is_plat("macosx") then
        add_files("$(projectdir)/embed/lazy100.S")
    end
target_end()
