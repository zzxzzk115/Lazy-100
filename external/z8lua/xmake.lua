-- z8lua: the PICO-8 Lua fork (fixed-point numbers + the p8 dialect built into the
-- lexer/parser). Vendored from fake-08's copy (jtothebell/z8lua); powers the p8 ext VM,
-- while native carts keep stock Lua 5.4 + sol2. lua_Number is the C++ fix32 class, so
-- the whole tree builds as C++.
target("z8lua")
    set_kind("static")
    set_languages("cxx17")
    set_default(false)
    add_files("*.c|ltests.c", {sourcekind = "cxx"})
    add_headerfiles("*.h")
    add_includedirs(".", {public = true})
    -- Prefix every exported Lua symbol with z8_ so this fork does not collide with the stock
    -- Lua 5.4 that sol2 links (wasm-ld rejects the duplicates; MSVC tolerated them). The header
    -- is force-included ahead of every source, and also into p8_vm.cpp (source/xmake.lua).
    add_forceincludes("z8prefix.h", {public = false})
    if is_plat("windows") then
        add_defines("_CRT_SECURE_NO_WARNINGS", "NOMINMAX", "WIN32_LEAN_AND_MEAN")
    end
target_end()
