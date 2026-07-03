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
    elseif is_plat("wasm") then
        -- No in-binary asset embedding on wasm: bake the font into MEMFS at /lazy100_font.ttf
        -- (read back via std::ifstream in embed.cpp), mirroring the .rc/.S delivery on desktop.
        add_ldflags("--embed-file=" .. path.join(os.projectdir(),
                    "assets/fonts/fusion-pixel-8px-monospaced-zh_hans.ttf") .. "@/lazy100_font.ttf",
                    {force = true})
        -- VRI's GL backend targets WebGL2 (GLES3) through Emscripten's GLFW3 + FULL_ES3 ports;
        -- FULL_ES3 supplies the ES3 entry points it calls (glMapBufferRange, glTexStorage2D/3D,
        -- glGenSamplers/glSamplerParameteri, glUniformBlockBinding, ...). force = true so the
        -- flag check doesn't silently drop these Emscripten -s flags.
        add_ldflags("-sUSE_GLFW=3", "-sFULL_ES3", "-sMIN_WEBGL_VERSION=2", "-sMAX_WEBGL_VERSION=2",
                    {force = true})
        -- Runtime environment, matching the libvultra/VultraEngine wasm apps: exceptions at link
        -- (VRI + Lua throw / longjmp), and a growable heap seeded large - the default 16 MB can't
        -- hold the ~4 MB font plus framebuffers, Lua, and the spirv-cross runtime.
        -- Emscripten's default 64 KB stack overflows during init (which manifests as a corrupted
        -- "null function" call): spirv-cross transpiles the present shader with deep recursion at
        -- pipeline creation, and VRI/Lua add more. Give it 4 MB, and a growable heap seeded large
        -- (the 16 MB default can't hold the ~4 MB font + framebuffers + Lua + spirv-cross runtime).
        add_ldflags("-fexceptions", "-sALLOW_MEMORY_GROWTH=1", "-sINITIAL_MEMORY=134217728",
                    "-sSTACK_SIZE=4194304", {force = true})
        -- Emscripten 6 backs growable memory with a *resizable* ArrayBuffer by default, and
        -- Chrome's TextDecoder.decode() rejects views of those - the embedded-files loader then
        -- throws during initRuntime and boot dies silently ("standby" forever). Fall back to the
        -- classic grow path (buffer swapped on grow, non-resizable) to keep TextDecoder happy.
        add_ldflags("-sGROWABLE_ARRAYBUFFERS=0", {force = true})
        -- Pixel-art page shell + a cartridge "slot": the shell's JS writes uploaded .png/.lz100
        -- files into MEMFS /carts (which the console's ls/load read), so carts can be played on
        -- the web. FORCE_FILESYSTEM + exporting FS make that reachable from the shell script.
        add_ldflags("--shell-file=" .. path.join(os.projectdir(), "web/shell.html"), {force = true})
        -- addRunDependency/removeRunDependency must be exported too: the shell's IDBFS preRun
        -- gates main() on them, and un-exported they are undefined there (boot then dies
        -- silently in preRun and the page never leaves "standby").
        add_ldflags("-sFORCE_FILESYSTEM=1",
                    "-sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency",
                    {force = true})
        -- explore downloads (emscripten_fetch) + persistent storage: /carts and /saves are
        -- IDBFS mounts (IndexedDB), loaded in preRun (see web/shell.html) and flushed after
        -- writes via vfs::persist_flush().
        add_ldflags("-sFETCH", "-lidbfs.js", {force = true})
    end

    -- GL-only build: no Vulkan loader is linked. (When the VRI Vulkan backend is enabled the
    -- host must link vulkan-1 from the system SDK - but do it by the loader's full path, never
    -- by adding $VULKAN_SDK/Lib as a search dir: that dir also ships MD-built spirv-cross libs
    -- that would shadow our MT spirv-cross and break the link with LNK2038.)
target_end()
