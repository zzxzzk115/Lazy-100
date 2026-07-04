# Lazy-100 Design Document

> 中文版见 [zh_CN/DESIGN.md](zh_CN/DESIGN.md)。

Lazy-100 is a **fantasy console** in the spirit of PICO-8 / TIC-80 / basic8, with a resolution
fixed at **320×240** (readable text), a 256-color palette, 4-channel chip audio, and carts
scripted in **Lua 5.4**. It **stands on the shoulders of VRI** (`../VRI`), a cross-backend RHI
(Vulkan / D3D12 / Metal / WebGPU / GL / GLES / WebGL).

The console is complete and self-hosting: everything — shell, code / sprite / map / sfx /
music-tracker editors, the online cart browser — runs inside the console itself, on desktop
(Windows / Linux / macOS) and on the web (Emscripten,
[zzxzzk115.github.io/Lazy-100](https://zzxzzk115.github.io/Lazy-100/)). It also runs p8 carts
natively through a vendored fixed-point **z8lua** VM. This document describes the architecture;
the cart-facing API lives in [CHEATSHEET.md](CHEATSHEET.md).

---

## 1. Tech Stack

| Responsibility | Choice | xmake package | Notes |
|------|------|----------|------|
| Rendering | **VRI** | `vri` | Cross-backend RHI; creates no window itself, only receives a native handle |
| Windowing + input | **SDL3** | `libsdl3` | `<vri/integration/vri_sdl3.h>` provides `vriWindowHandleFromSDL3()` |
| Audio | **miniaudio** | `miniaudio` | Header-only; `MINIAUDIO_IMPLEMENTATION` in exactly one TU (audio.cpp) |
| Script binding | **sol2** | `sol2` | C++ ↔ Lua binding layer (native carts) |
| Script runtime | **Lua 5.4** | `lua` | Native carts; portable incl. the WASM target |
| p8 runtime | **z8lua** (vendored) | — | Fixed-point Lua fork with the p8 dialect; `external/z8lua` |

All external packages come from the custom repo `https://github.com/zzxzzk115/xmake-repo.git`,
so VRI is consumed as a plain `add_requires("vri")` like any other dependency.

---

## 2. Module Layout

Layering principle: **video (pure CPU pixels) → gpu (the only thing coupled to VRI) → console
(owns the loop) → script (the only thing coupled to a VM)**. `video/` depends on neither
SDL/VRI/Lua, so headless tools (`cartshot`, `cartwav`) and tests link it directly.

```
source/lazy100/
  common/          types, logging, letterbox layout math
  video/           palette, 320×240 index framebuffer, draw ops, font (TIC-80 latin bitmap +
                   Fusion Pixel 8px CJK via stb_truetype), sprites, pixel cursor, icons
  gpu/             present.* — the only VRI code: index texture upload + palette resolve
  audio/           4-channel synth: sfx patterns + song sequencer, speaker warm-up,
                   web background device suspend/rebuild; offline WAV render
  script/          lua_runtime.* (sol2, native carts) · p8_vm.* (z8lua, p8 carts) ·
                   lua_api.* (shared C++ API surface) — dual-VM routing per cart language
  cart/            .lz100 text format (code+gfx+flags+map+sfx+music+label+title/author),
                   cartpng.* — shareable .lz100.png cartridge (payload in low 2 bits)
  input/           input.* 6-button pad · keyboard.* full keys · mouse.* — all three accept
                   web-injected state (touch gamepad / on-screen keyboard / canvas trackpad)
  world/           128×64 tile map
  shell/           command line (help/ls/cd/load/save/run/title/author/…)
  editor/          editor host + code / sprite / map / sfx / music editors, shared ui/icons
  explore/         online cart browser for the Lazy-100-games catalog
  net/             fetch (native curl / emscripten_fetch)
  vfs/             embedded assets (font) + persistent saves (IDBFS on web)
  console/         orchestrator: mode state machine, boot ceremony, pause menu,
                   love2d-style error screen, cart lifecycle; window.* (SDL3)

source/lazy100-app/  host binary + the wasm C exports the web site drives
tools/               cartshot (headless first-frame PNG) · cartwav (headless music WAV)
web/site/            the two-page site (home = full console, carts = catalog grid)
external/z8lua/      vendored fixed-point Lua for p8 carts
```

---

## 3. Console Modes & Frame Loop

The console is a mode state machine driven by a fixed-timestep loop (logic 30 Hz, or 60 Hz if
the cart defines `_update60`; present follows vsync):

```
Boot ──splash──▶ Shell ◀──ESC──▶ Editor ◀─┐
                  │  ▲                    │ pause menu
                  ▼  │                    │ (continue / reset / edit / explore / shell)
               Explore ────▶ Running ◀────┘
```

- **Boot**: power-on splash + audio warm-up; on the web it doubles as the press-any-key
  gate that unlocks the AudioContext (a cart can be *armed* to start from the gate gesture).
- **Running**: fixed-step `_update`/`_update60` + `_draw`; ESC opens the pause menu. A script
  error halts the cart on a blue error screen (core message + "press ESC to edit and fix it");
  the code editor shows the same error in an inline bar until the next clean run.
- **Editor**: tabbed suite (code/sprite/map/sfx/music), Ctrl+Tab cycles.
- Cart language is detected at load: `.lz100`/`.lua` → sol2 + Lua 5.4; p8 carts → z8lua with
  the real fixed-point dialect (no transpilation).

---

## 4. Present: R8_UINT Index Texture + Palette Resolve

The CPU framebuffer is `uint8[320*240]`. Each frame it is memcpy'd into a staging ring and
uploaded as an `R8_UINT` texture; a full-screen-triangle fragment shader resolves color through
a 256-entry palette constant buffer (updated only when dirty). Integer textures are fetched
with `.Load` — no filtering, which pairs exactly with integer-scaled nearest-neighbor output
into a centered letterbox (black bars cleared by the render pass).

Why not expand to RGBA on the CPU: 4× upload bandwidth, and palette effects (`pal()` swaps,
fades, cycling) would rewrite 76800 pixels instead of 256 uniform entries.

The swapchain tracks the window (`drawable_size`) every frame and resizes itself; on the web
the canvas must be resized **through SDL** (`lazy100_resize` → `SDL_SetWindowSize`) so SDL,
the canvas backing store and the letterbox math never disagree.

Shaders are precompiled offline (Slang → SPIR-V / DXBC / WGSL headers, generated with VRI's
`vri-shaderc`) and committed, so ordinary builds need no shader toolchain.

---

## 5. Cart Formats

- **`.lz100`** — a single plain-text file with sections (`__lua__`, `__gfx__`, `__gff__`,
  `__map__`, `__sfx__`, `__music__`, `__label__`) plus `title` / `author` header lines.
  Everything the editors produce round-trips through it.
- **`.lz100.png`** — the shareable cartridge: a rendered cartridge image (header band, 320×240
  screenshot at a fixed offset, title/author footer) with the RLE-compressed `.lz100` text
  hidden in the low 2 bits of the RGBA pixels. The image doubles as the catalog preview.
- **p8 carts** — text and PNG forms load through the same pipeline and route to z8lua.
  p8 is a compatibility feature; the public catalog carries `.lz100.png` carts only.

---

## 6. Input

Three input surfaces, all with a web-injection path (the touch site drives them via C exports):

| Surface | Native source | Web injection |
|---|---|---|
| `Input` (6-button pad) | SDL scancodes (arrows + Z/X/C/V) | `lazy100_set_pad` mask |
| `Keyboard` (editors/shell) | SDL key state + text input | `lazy100_set_keys` mask + `lazy100_type_text` |
| `Mouse` (editors) | SDL pointer via letterbox transform | `lazy100_set_mouse` (canvas-as-trackpad) |

Pad edges (`btnp`) are sampled per **logic step** (not per render frame) with the classic
15/4-frame auto-repeat, so taps behave identically at any refresh rate.

---

## 7. Audio

A 4-channel chip synth in the miniaudio render callback: 64 sfx patterns (32 steps × pitch /
wave / volume / fx) and a 64-row song sequencer; sfx voices mask music voices per-channel and
the music keeps stepping silently underneath so it resumes in sync. A ~1.5 s sub-audible dither
warms power-saving speakers after device start.

Web specifics: the device starts inside the first user-gesture stack (AudioContext unlock);
when the tab backgrounds, the site auto-pauses the cart and **kills the device**
(`lazy100_audio_suspend`), rebuilding it fresh on return — resuming a backgrounded iOS
AudioContext is unreliable, a fresh one is not. `cartwav` renders the sequencer headlessly to
WAV for analysis.

---

## 8. Web Site & Exports

`web/site/` is a two-page static site (home = the full console; `/carts/` = catalog grid that
links back to `/?cart=<id>`), deployed by pushing the `doc` branch (GitHub Actions builds the
wasm console with `scripts/build_site.sh`). The host app exports the C hooks the site uses:
cart boot/arm, pad/keyboard/text/mouse injection, mode query (to swap touch control sets),
canvas resize, background pause, audio suspend/resume/rewarm. Mobile shows a virtual gamepad
while a cart runs and an on-screen keyboard + canvas trackpad in the shell/editors.

---

## 9. Build & Tooling

xmake end to end. `xmake` builds the host; `xmake f -p wasm && xmake build lazy100` the web
console; `scripts/build_site.sh` assembles the whole site into `build/site/`. Host tools:
`cartshot` (headless first-frame `.png` / packed `.lz100.png`) and `cartwav`. Tests live under
`tests/` (`lazy100_build_tests=n` to skip). Windows uses the static CRT (MT) to match VRI.
CI: `deploy_pages.yaml` (site, on `doc` push) and `release_prebuilt.yaml` (console + tools
zips per platform, on release publish).
