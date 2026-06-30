-- Third-party dependencies for Lazy-100.
--   vri:       cross-API RHI (Vulkan backend by default); window handle via the SDL3 integration header
--   libsdl3:   window creation + keyboard/gamepad input
--   miniaudio: audio output (header-only)
--   lua 5.4:   script runtime
--   sol2:      C++ <-> Lua binding (header-only, compiled against the lua above)

-- VRI's ecosystem uses the static CRT on Windows. Force every package to the same runtime
-- as this project (MTd in debug / MT in release) or vri.lib fails to link (LNK2038).
if is_plat("windows") then
    add_requireconfs("**", {configs = {runtimes = is_mode("debug") and "MTd" or "MT"}})
end

-- vri builds with the static debug CRT (MTd) only in a debug package build; follow the
-- project mode so its _ITERATOR_DEBUG_LEVEL matches ours (avoids the IDL 0-vs-2 mismatch).
add_requires("vri", {debug = is_mode("debug")})
add_requires("libsdl3")
add_requires("miniaudio")
add_requires("lua 5.4")
add_requires("sol2")
add_requires("stb") -- stb_truetype: runtime glyph rasterization (CJK + Latin)
