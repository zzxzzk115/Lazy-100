# Lazy-100 TODO (Runtime Kernel / MVP v1)

> 中文版见 [zh_CN/TODO.md](zh_CN/TODO.md)。

Vertical-slice milestones — **at the end of every milestone you can run `xmake && xmake run lazy100 examples/carts/<cart>.lua`**.
M0/M1 retire VRI-integration and GPU risk before building the Lua/gameplay layer.

See [DESIGN.md](DESIGN.md) for details.

---

## M0 — Black Window (boot/present backbone)

- [ ] Root `xmake.lua`: `PROJECT_NAME` → `lazy100`, update README
- [ ] `add_requires` vri / libsdl3 / miniaudio / lua 5.4 / sol2 (VRI is now a custom-repo package — plain `add_requires("vri")`)
- [ ] `console/window.*`: SDL3 window create + event pump + `vriWindowHandleFromSDL3()`
- [ ] `gpu/present.*`: VRI device + swapchain creation; debug validation routed to `common/log`
- [ ] Per frame acquire → clear backbuffer → present; handle `OutOfDate`/resize/minimize
- [ ] `examples/run/main.cpp`: `Console c; c.Boot(); c.Run();`
- **Acceptance**: a window pops up, stably showing a solid-color clear, and can be closed

## M1 — Framebuffer On-Screen (retire GPU risk)

- [ ] `video/palette.*`: 32-entry RGBA8 default palette
- [ ] `video/framebuffer.*`: 320×240 uint8 index buffer + `cls/pset/rectfill`
- [ ] `gpu/shaders/present.slang`: full-screen-triangle VS + palette-resolve PS; generate `present_spv.h` (+ `_dxbc.h` as needed) with `vri-shaderc`
- [ ] `present.*`: R8_UINT index texture + staging-ring upload + palette CBV + NEAREST sampler + full-screen quad
- [ ] `GetFormatSupport(R8_UINT)` validation at boot (with R8_UNORM fallback)
- [ ] Integer-scaled letterbox viewport
- [ ] Hard-coded C++ test pattern + asymmetric sprite to verify Y-flip direction
- **Acceptance**: the 320×240 test pattern is correctly upscaled on-screen, right-side-up, with no blurring

## M2 — Lua Drawing

- [ ] `script/lua_runtime.*`: sol::state, load `.lua` cart, resolve `_init`/`_draw` (`sol::protected_function` error containment)
- [ ] `video/draw.*`: line/rect/rectfill/circ/circfill/clip/camera
- [ ] `video/font.*`: 1bpp bitmap font + `print`
- [ ] `script/lua_api.*`: bind cls/pset/pget/line/rect/rectfill/circ/circfill/clip/camera/print + math (flr/min/max/rnd/sin/cos...)
- [ ] `cart/cart.*`: load a pure `.lua` from disk
- [ ] `examples/carts/hello.lua`
- **Acceptance**: a cart draws shapes and text in Lua and displays them

## M3 — Loop + Input

- [ ] `console/console.*`: fixed-timestep accumulator (30Hz, `_update60` → 60Hz), `min(dt,0.25)` clamp
- [ ] `input/input.*`: 6-button virtual gamepad, SDL3 keymap, `held/prev/pressed` masks, per-step edges, auto-repeat (every 4 after 15)
- [ ] Bind `btn`/`btnp`, wire up `_update`
- [ ] `examples/carts/bounce.lua` (interactively movable dot)
- **Acceptance**: arrow keys/Z/X respond live, btnp fires exactly once on a single-frame tap

## M4 — Sprites + Font

- [ ] `video/sprites.*`: 128×128 index sprite sheet + `spr`/`sspr`/`sget`/`sset`, flip
- [ ] `pal`/`palt` transparency (index 0 transparent by default)
- [ ] Bind spr/sspr/sget/sset/fget/fset/pal/palt
- [ ] Sprite data source: sset() or side-car indexed PNG load
- [ ] `examples/carts/sprite.lua`
- **Acceptance**: sprites draw correctly with transparency, flip / palette remap take effect

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
