# Lazy-100 TODO (Runtime Kernel / MVP v1)

> 中文版见 [zh_CN/TODO.md](zh_CN/TODO.md)。

Vertical-slice milestones — **at the end of every milestone you can run `xmake && xmake run lazy100 examples/carts/<cart>.lua`**.
M0/M1 retire VRI-integration and GPU risk before building the Lua/gameplay layer.

See [DESIGN.md](DESIGN.md) for details.

---

## M0 — Black Window (boot/present backbone) ✅

- [x] Root `xmake.lua`: `PROJECT_NAME` → `lazy100`, update README
- [x] `add_requires` vri / libsdl3 / miniaudio / lua 5.4 / sol2 (VRI is now a custom-repo package — plain `add_requires("vri")`)
- [x] `console/window.*`: SDL3 window create + event pump + `vriWindowHandleFromSDL3()`
- [x] `gpu/present.*`: VRI device + swapchain creation; debug validation routed to `common/log`
- [x] Per frame acquire → clear backbuffer → present; handle `OutOfDate`/resize/minimize
- [x] `examples/run/main.cpp`: `Console c; c.Boot(); c.Run();`
- **Acceptance**: a window pops up, stably showing a solid-color clear, and can be closed

> Build note (Windows): the project uses the **static CRT (MT/MTd)** to match the VRI
> package, and links the Vulkan loader from the system Vulkan SDK in `examples/xmake.lua`.

## M1 — Framebuffer On-Screen (retire GPU risk) ✅

- [x] `video/palette.*`: 32-entry RGBA8 default palette (PICO-8 16 + extended 16)
- [x] `video/framebuffer.*`: 320×240 uint8 index buffer + `cls/pset/rectfill`
- [x] `gpu/shaders/present.slang`: full-screen-triangle VS + palette-resolve PS; generated `present_spv.h` with the prebuilt `vri-shaderc`
- [x] `present.*`: R8_UINT index texture + staging-ring upload + palette uniform + full-screen triangle (texelFetch `.Load`, so **no sampler** needed)
- [x] `GetFormatSupport(R8_UINT)` check at boot (UNORM-fallback shader deferred — R8_UINT is universal on desktop Vulkan)
- [x] Integer-scaled letterbox viewport
- [x] Hard-coded C++ test pattern (palette bars + top/left edge markers) to verify orientation
- **Acceptance**: the 320×240 test pattern is correctly upscaled on-screen, right-side-up, with no blurring

> Gotchas: (1) Slang defaults `Texture2D<uint>` to a SPIR-V `R32ui` image format → mismatch
> vs the R8_UINT view (undefined fetches); pin it with `[[vk::image_format("r8ui")]]`.
> (2) **VRI clip space is Y-up** — flip `pos.y` in the present VS or the image is upside down.

## M2 — Lua Drawing ✅

- [x] `script/lua_runtime.*`: sol::state, load `.lua` cart, resolve `_init`/`_draw` (`sol::protected_function` error containment)
- [x] `video/draw.*`: line/rect/circ/circfill (cls/pset/pget/rectfill live on Framebuffer); clip/camera deferred to M4
- [x] `video/font.*`: proportional bitmap font (Quaver, CC BY 3.0) baked by `tools/genfont.ps1` + `print`
- [x] `script/lua_api.*`: bind cls/pset/pget/line/rect/rectfill/circ/circfill/print + math (flr/ceil/abs/min/max/mid/sgn/sqrt/sin/cos/atan2/rnd/srand/t)
- [x] cart loading: `lua_runtime.load_cart()` runs a pure `.lua` from disk (dedicated `cart/` module deferred until the cart format matters)
- [x] `examples/carts/hello.lua`
- **Acceptance**: a cart draws animated shapes + text in Lua and displays them ✅

## M3 — Loop + Input ✅

- [x] `console/console.*`: fixed-timestep accumulator (30Hz, `_update60` → 60Hz), `min(dt,0.25)` clamp
- [x] `input/input.*`: 6-button virtual gamepad, SDL3 keymap (arrows + Z/C, X/V), `held/prev/pressed` masks, per-step edges, auto-repeat (every 4 after 15)
- [x] Bind `btn`/`btnp`, wire up `_update`
- [x] `examples/carts/bounce.lua` (interactively movable dot)
- **Acceptance**: arrow keys/Z/X respond live, btnp fires exactly once on a single-frame tap ✅

> Gotcha: Lua numbers are doubles, and sol2 refuses float→int args. Bind draw/input
> coordinate params as `double` and `floor()` them (PICO-8 coordinate semantics).

## M4 — Sprites + Font ✅

- [x] `video/sprites.*`: 128×128 index sprite sheet + `spr`/`sspr`/`sget`/`sset`/`fget`/`fset`, flip
- [x] `pal`/`palt` transparency (index 0 transparent by default) + draw/screen palette remap
- [x] Bind spr/sspr/sget/sset/fget/fset/pal/palt
- [x] Sprite data source: `sset()` from Lua (side-car PNG load deferred)
- [x] `examples/carts/sprite.lua`
- **Acceptance**: sprites draw correctly with transparency, flip / palette remap take effect ✅

### Font → runtime CJK rasterization (replaces the baked bitmap font)

- [x] `video/font.*` rewritten to rasterize glyphs on demand with **stb_truetype** + cache
- [x] UTF-8 decode in `print`; half-width Latin + full-width 中日韩 advance from font metrics
- [x] Load the [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) (pan-CJK, OFL) at boot
- [x] `examples/carts/cjk.lua` — verified Chinese / Japanese / Korean render crisp
- [x] `/utf-8` MSVC flag for UTF-8 source; `flr`/`ceil` return integers

## M5 — Audio Seam

- [ ] `audio/audio.*`: miniaudio device + callback (`MINIAUDIO_IMPLEMENTATION` in this TU only)
- [ ] `sfx()` lock-free queue: Lua pushes id → callback consumes → square-wave beep
- [ ] Bind `sfx`/`music` (music is a no-op stub)
- **Acceptance**: a cart calls `sfx(0)` and you hear a beep; the queue boundary is the hook for a future tracker

---

## Later Versions (not in v1)
- In-console editors: code editor, sprite editor, map editor, music tracker
- Cart packaging format (`__lua__`/`__gfx__`/`__sfx__` sections)
- Full audio synth / tracker
- N-deep frames-in-flight rendering optimization
- WASM/Web target
