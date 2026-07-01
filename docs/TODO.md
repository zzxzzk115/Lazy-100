# Lazy-100 TODO (Runtime Kernel + Editor Suite V1)

> 中文版见 [zh_CN/TODO.md](zh_CN/TODO.md)。

Vertical-slice milestones — **at the end of every milestone you can run `xmake && xmake run lazy100 carts/<cart>.lua`** (or `xmake run lazy100` with no cart to boot into the shell).
M0/M1 retire VRI-integration and GPU risk before building the Lua/gameplay layer; M6+ add the in-console editors.

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

- [x] `video/palette.*`: 256-entry RGBA8 default palette (32 curated + 6×6×6 color cube + grayscale ramp)
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

- [x] `video/sprites.*`: 256×256 index sprite sheet (16×16 grid of 16px sprites) + `spr`/`sspr`/`sget`/`sset`/`fget`/`fset`, flip
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

## M5 — Audio Seam ✅

- [x] `audio/audio.*`: miniaudio device + callback (`MINIAUDIO_IMPLEMENTATION` in this TU only)
- [x] `sfx()` lock-free SPSC queue: Lua pushes id → callback consumes → square-wave voices (pentatonic notes, decaying envelope, 4 voices)
- [x] Bind `sfx`/`music` (music is a no-op stub)
- **Acceptance**: bounce.lua beeps on Z/X; the queue boundary is the hook for a future tracker ✅

---

# Editor Suite V1 (in-console authoring + cart format)

The runtime kernel can *play* a cart; V1 makes the console *self-contained* — author code,
sprites, maps, and music inside it and save/load a single shareable `.lz100`.

## M6 — Shell + mode system + full input ✅

- [x] `console/window.*`: pump text input (`SDL_EVENT_TEXT_INPUT`) + mouse motion/buttons/wheel
- [x] `input/keyboard.*`: full-keyboard edges + auto-repeat + typed UTF-8 (distinct from the game `Input`)
- [x] `input/mouse.*`: cursor mapped into the 320×240 framebuffer via the letterbox transform + button edges
- [x] `console/console.*`: `ConsoleMode {Shell, Running, Editor}` dispatch; ESC toggles
- [x] `shell/shell.*`: Linux-like command line (cd/pwd/ls/help/tab-completion/history), sandboxed to `carts/`
- [x] `editor/editor.*`: `Editor` interface + `EditorHost` tab bar
- **Acceptance**: boot into the shell, type/`ls`; ESC into the tabbed editor ✅

## M7 — Cart model + `.lz100` load/save ✅

- [x] `cart/cart.*`: PICO-8-style single text file, section parse/serialize
- [x] Run from `Console::code()` (not a file); restore sprite sheet on load
- [x] Shell `load`/`save`/`run`/`new`; assets embedded in the binary via `.rc`/`.S` + in-memory VFS
- **Acceptance**: `load demo.lz100` → `run`; `save` round-trips code + sprites ✅

## M8 — Sprite editor ✅

- [x] `editor/sprite_editor.*`: magnified canvas, palette grid, sheet navigator, eyedropper
- **Acceptance**: paint a sprite → `run` a cart that `spr()`s it; persists in `__gfx__` ✅

## Spec upgrade — a notch above PICO-8 ✅

- [x] Palette 32 → **256** colors; sprites 8×8 → **16×16**; sheet 128×128 → **256×256**
- [x] Rippled through palette default, present shader (`uint4[64]`), sprites, cart `__gfx__`, sprite editor

## M9 — Map + map runtime ✅

- [x] `world/map.*`: 128×64 tiles; Lua `mget`/`mset`/`map(cx,cy,sx,sy,cw,ch)` (16px tiles)
- [x] `editor/map_editor.*`: scrolling viewport (paint/erase, arrow-key pan), sprite picker, tile preview
- [x] cart `__map__` section
- **Acceptance**: paint a map → cart `map()` renders it; round-trips ✅

## M10 — Code editor ✅

- [x] `editor/code_editor.*`: line buffer, cursor/scroll/insert/delete/newline/Tab, line-number gutter, caret
- [x] UTF-8 aware (cursor steps whole codepoints — 中日韩 edit correctly); stays in sync with `Console::code()`
- **Acceptance**: edit Lua in-console → ESC → shell `run` picks up the change ✅

## M11 — Music/sfx tracker + audio synth ✅

- [x] `audio/sound.*`: `SfxPattern` (32 steps: pitch/wave/vol/effect + speed), `MusicPattern` (4 channels), `SoundBank`
- [x] audio engine: 5 waveforms (square/pulse/triangle/saw/noise), click-free envelope, chromatic pitch, 4-channel + music sequencer
- [x] Thread-safe handoff: sfx snapshot over the SPSC queue; music via an atomically-flipped SoundBank snapshot
- [x] cart `__sfx__`/`__music__`; Lua `sfx(n[,chan])` / `music(n)` / `music(-1)`
- [x] `editor/sfx_editor.*` (piano-roll grid + wave/vol/speed pickers + preview), `editor/music_editor.*` (4-channel arrange + transport)
- **Acceptance**: author an sfx → preview/`sfx(0)`; arrange music → `music(0)`; round-trips ✅

---

## Beyond V1 (not yet)
- Syntax highlighting + horizontal scroll in the code editor; monospaced code face
- Sfx effects (slide/vibrato/fades) beyond the reserved slots
- Copy/paste across sprite/map/tracker; undo
- N-deep frames-in-flight rendering optimization
- WASM/Web target
