# Lazy-100 Design Document (Runtime Kernel / MVP v1)

> 中文版见 [zh_CN/DESIGN.md](zh_CN/DESIGN.md)。

Lazy-100 is a **fantasy console**, in the spirit of PICO-8 / TIC-80 / basic8, but with a resolution fixed at **320×240** (128×128 is too small to read text), scripted in **Lua 5.4**. It **stands on the shoulders of VRI** — VRI (`../VRI`) is a cross-backend RHI (Render Hardware Interface, abstracting Vulkan / D3D12 / WebGPU / GL).

This document describes the architecture of the **runtime kernel (v1)**. The in-console editors (code / sprite / map / music tracker) belong to later versions and are out of scope here.

---

## 1. Tech Stack

| Responsibility | Choice | xmake package | Notes |
|------|------|----------|------|
| Rendering | **VRI** | `vri` | Cross-backend RHI; creates no window itself, only receives a native handle |
| Windowing + input | **SDL3** | `libsdl3` | `<vri/integration/vri_sdl3.h>` provides `vriWindowHandleFromSDL3()` |
| Audio | **miniaudio** | `miniaudio` | Header-only; `MINIAUDIO_IMPLEMENTATION` in exactly one TU |
| Script binding | **sol2** | `sol2` | C++ ↔ Lua binding layer |
| Script runtime | **Lua 5.4** | `lua` | Standard, portable (including future WASM/Web targets) |

> The user's initial idea was "VRI + miniaudio + sol2". Filling the gaps: sol2 is only a binding layer and needs **lua** itself; VRI neither opens a window nor handles input, so **SDL3** is needed to provide the window handle and keyboard/gamepad input.

All 5 packages above are already available in the custom repo `https://github.com/zzxzzk115/xmake-repo.git` — VRI included, so it is consumed as a plain `add_requires("vri")` like any other dependency.

---

## 2. Module Layout

Layering principle: **video (pure CPU pixels) → gpu (the only thing coupled to VRI) → console (owns the loop) → script (the only thing coupled to sol2)**.
`video/` depends on neither SDL/VRI/Lua, so it can be unit-tested headless and dump BMPs; `present.cpp` is the only file that touches VRI, making it easy to swap backends later or add an SDL_Renderer fallback.

```
source/lazy100/
  common/
    types.hpp        Fixed-width aliases, Color32, Rect, small math, LZ_ASSERT
    log.hpp          Logging; also the sink for VRI's MessageCallback
  video/             —— Deterministic, headless-unit-testable; no SDL/VRI/Lua deps
    palette.*        32-entry RGBA8 palette (default PICO-8 style), set/get/reset
    framebuffer.*    320×240 uint8 index buffer + clip rect + camera offset
    draw.*           cls/pset/pget/line/rect/rectfill/circ/circfill/clip
    font.*           1bpp bitmap glyph table (ASCII 32..126), print() rasterization
    sprites.*        Sprite sheet (128×128 indices = 16×16 of 8px sprites), spr()/sspr()
    blit.hpp         Inline span/pixel helpers (clip + transparent-index handling)
  gpu/               —— The only layer that touches VRI
    present.*        VRI device+swapchain owner; uploads index texture + full-screen palette resolve
    shaders/
      present.slang  Full-screen-triangle VS + palette-resolve PS
      present_spv.h  Generated: g_presentSpv (Vulkan / GL via SPIRV-Cross)
      present_dxbc.h Generated (D3D12, Windows host)
      present_wgsl.h Generated (optional in v1)
  audio/
    audio.*          The only definition of MINIAUDIO_IMPLEMENTATION; mixer + sfx() lock-free queue
  script/            —— The only layer that #includes <sol/sol.hpp>
    lua_api.*        sol2 binds draw/input/audio onto Console&
    lua_runtime.*    Owns sol::state; loads cart; resolves/calls _init/_update/_draw
  cart/
    cart.*           Loads .lua (v1 = pure script; embedded sheet/palette deferred)
  input/
    input.*          6-button virtual gamepad; btn/btnp bitmasks; SDL3 keymap; per-step edges
  console/
    console.*        Orchestrator: owns Window/Present/Framebuffer/Sprites/Input/Audio/Lua
    window.*         SDL3 window create/destroy, event pump, vriWindowHandleFromSDL3()
    config.hpp       LZ_W=320, LZ_H=240, target fps, palette size, sprite-sheet size

examples/
  run/main.cpp       argv[1]=cart path; Console c; c.Boot(); c.Run();
  carts/             hello.lua, bounce.lua ...
tests/               Optional v1: host-only unit tests for video/ (no GPU)
external/xmake.lua   VRI dependency wiring
```

---

## 3. Frame Loop (Fixed Timestep)

The Console owns the main loop. Logic uses a fixed-step accumulator; rendering follows vsync.

```text
Console::Run():
  lua.call_init()                      # cart _init() called once
  prev = clock::now(); acc = 0
  STEP = 1.0 / target_fps              # default 1/30; 1/60 if the cart defines _update60
  while running:
      now = clock::now()
      frame_dt = min(now - prev, 0.25) # guard against the spiral of death
      prev = now; acc += frame_dt
      window.pump_events(running)      # SDL_PollEvent; snapshot raw key state into Input
      while acc >= STEP:               # fixed-step update, 0..N times
          input.begin_step()           # pressed = held & ~prev_held
          lua.call_update()            # game logic + btn/btnp reads
          input.end_step()             # prev_held = held
          acc -= STEP
      lua.call_draw()                  # cart writes the Framebuffer via the draw API
      present.submit_frame(framebuffer, palette)
```

---

## 4. present.submit_frame — VRI Sequence

Aligned with `VRI/examples/common/example_app.h`: acquire → barrier → upload → full-screen draw → present → fence-wait.

```text
present.submit_frame(fb, pal):
  if pal.dirty: map paletteBuf; memcpy 32×RGBA8; unmap; dirty=false
  memcpy(MapBuffer(stagingRing[frame%N]), fb.pixels(), 320*240); UnmapBuffer

  AcquireNextTexture(swapchain, &index)        # handle OutOfDate → swap.Resize and skip
  bbView = CreateTextureView(backbuffers[index])

  BeginCommandBuffer(cmd)
    CmdBarrier(indexTex: * -> CopyDestination)
    CmdUploadBufferToTexture(indexTex <- staging)   # tightly packed 320×240 R8; VRI handles row pitch
    CmdBarrier(indexTex: CopyDestination -> ShaderResource @ FragmentShader)
    CmdBarrier(backbuffer: Undefined -> ColorAttachment)
    CmdBeginRendering({color=bbView, loadOp=Clear(black bars)})
      CmdSetViewports(integer-scaled letterbox rect); CmdSetScissors(full window)
      CmdSetPipelineLayout(presentLayout); CmdSetPipeline(presentPipeline)
      CmdSetDescriptorSet(0, {indexTex SRV, palette CBV, NEAREST sampler})
      CmdDraw({vertexNum=3})                          # full-screen triangle, no vertex buffer
    CmdEndRendering()
    CmdBarrier(backbuffer: ColorAttachment -> Present)
  EndCommandBuffer(cmd)
  QueueSubmit(queue, {cmd, signal fence=++frameValue})
  Wait(fence, frameValue)                             # v1: simple CPU-GPU sync (see Risk 4)
  Present(swapchain)
  DestroyDescriptor(bbView)
```

Keep an N=image-count staging **ring buffer** to avoid per-frame reallocation.

---

## 5. Pixel Format: Upload an R8_UINT Index Texture + Resolve the Palette in the Fragment Shader

**Conclusion: take option (b)** — upload an `R8_UINT` index texture and resolve the color in the fragment shader using a 32-entry palette uniform. Do **not** expand to RGBA8 on the CPU every frame.

- Index texture: `VriTextureDesc{ 2D, R8_UINT, 320×240, usage=ShaderResource|copy-dst, Device }`. The CPU framebuffer is exactly `uint8_t[320*240]` (75 KB), memcpy'd into HostUpload staging, then `CmdUploadBufferToTexture`.
- Palette: 32 entries as a **constant buffer** (`uint32 palette[32]` RGBA8 packed), bound as a CBV, updated only when `dirty` (usually zero-cost).
- Fragment shader: `Texture2D<uint>` uses `.Load(int3(p,0))` texelFetch (integer textures cannot be linearly filtered — which pairs perfectly with nearest-neighbor upscaling), and outputs `palette[idx]` as RGBA.

**Rationale (vs option a: CPU-side per-frame index→RGBA8 expansion)**
- **Bandwidth**: 75 KB/frame vs 300 KB/frame — 4× savings.
- **Palette effects are nearly free**: `pal()` swaps, palette cycling, fades, screen flashes = changing 32 uniform entries, rather than rewriting 76800 pixels. This is exactly the fantasy-console idiom, and exactly the pain point of option (a).
- **CPU stays cheap**: the rasterizer always writes just 1 byte/pixel.

**Pitfall handled**: validate `GetFormatSupport(R8_UINT)` at boot; if a backend cannot sample integer textures, fall back to `R8_UNORM` + `round(s*255)`, with the change isolated to `present.slang` + one format constant.

---

## 6. Lua API (v1)

Global free functions (PICO-8 convention: a cart calls `pset(...)`, not `lz.pset(...)`). Colors = palette indices `0..31`.

```text
-- Lifecycle (defined by cart, called by kernel)
_init()   _update() / _update60()   _draw()

-- Graphics (write the index framebuffer)
cls([c])  pset(x,y,[c])  pget(x,y)->c
line(x0,y0,x1,y1,[c])  rect(x0,y0,x1,y1,[c])  rectfill(...)  circ(x,y,r,[c])  circfill(...)
clip([x,y,w,h])  camera([dx,dy])
pal([c0,c1])    -- draw-color remap; pal() resets
palt([c,t])     -- index c transparent (index 0 transparent by default)

-- Text
print(text,[x],[y],[c])->nextx    cursor(x,y,[c])

-- Sprites
spr(n,x,y,[w=1],[h=1],[flip_x],[flip_y])
sspr(sx,sy,sw,sh,dx,dy,[dw],[dh],[fx],[fy])
sget(x,y)->c   sset(x,y,[c])   fget/fset (v1 stub: sprite flags)

-- Input
btn([i],[p=0])->bool    -- i ∈ 0..5 (L,R,U,D,O/Z,X); no arg -> bitmask
btnp([i],[p=0])->bool   -- edge on the step it was pressed, with auto-repeat

-- Audio (v1 stub path)
sfx(n,[chan],[off])     -- routes to audio.trigger_sfx(n)
music(n)                -- v1 no-op

-- Math / utility
flr ceil abs min max mid sgn sqrt sin cos atan2 rnd srand t
```

**sol2 bindings** (`lua_api.cpp`, thin trampolines into video/input):

```cpp
void bind_api(sol::state& L, Console& con) {
    auto& fb = con.framebuffer(); auto& spr = con.sprites(); auto& in = con.input();
    L.set_function("cls",  [&fb](sol::optional<int> c){ fb.cls(c.value_or(0)); });
    L.set_function("pset", [&fb](int x,int y,sol::optional<int> c){ fb.pset(x,y,c); });
    L.set_function("spr",  [&spr,&fb](int n,int x,int y,sol::optional<int> w,sol::optional<int> h,
                                      sol::optional<bool> fx,sol::optional<bool> fy){
        spr.draw(fb,n,x,y,w.value_or(1),h.value_or(1),fx.value_or(false),fy.value_or(false)); });
    L.set_function("btn",  [&in](sol::optional<int> i,sol::optional<int> p){
        return i ? in.held(*i,p.value_or(0)) : in.held_mask(p.value_or(0)); });
}
```

`lua_runtime.cpp` caches callbacks as `sol::protected_function`; when a cart errors it prints diagnostics and freezes that cart rather than crashing the host:

```cpp
sol::protected_function cb_update = L["_update"];   // may be nil
// each step: if (cb_update.valid()) { auto r = cb_update(); if(!r.valid()) report_lua_error(r); }
```

---

## 7. Fixed Timestep & Input Model

**FPS decision: logic fixed-step defaults to 30 Hz, present follows vsync.** PICO-8 offers both `_update()`@30 and `_update60()`@60. A fixed 30 Hz is the classic feel, halves CPU cost, and makes `btnp` auto-repeat timing behave as expected. If a cart defines `_update60`, STEP=1/60. Decoupling logic from present matters because present is locked to vsync while the display may be 60/120/144 Hz.

**Input (`input/input.cpp`)** — six virtual keys, player 0 only in v1:
- `held_mask`: rebuilt each pump from SDL3 keyboard state (arrow keys + Z/X, configurable keymap), read live by `btn`.
- `prev_mask`: the held mask at the previous **logic step**.
- `pressed_mask = held_mask & ~prev_mask` → `btnp`, computed in `begin_step()`, **sampled per logic step, not per render frame**. Crucial: with 60 Hz render + 30 Hz logic, a one-frame tap must fire exactly once, and each of the N catch-up steps within a render frame must see correct press semantics.
- auto-repeat (PICO-8: after a 15-frame delay, every 4 frames): each key maintains a held-step counter and emits a repeat pulse at the corresponding interval.

---

## 8. Shader Processing

VRI consumes **precompiled** bytecode (Slang → SPIR-V/WGSL/DXBC offline; see VRI's `tools/vri-shaderc` and `xmake/tasks/shaders.lua`). Lazy-100 copies this pattern.

**v1 needs only one program**: `present.slang` — a full-screen-triangle VS (generates 3 clip-space vertices from `SV_VertexID`, no vertex buffer) + a palette-resolve PS:

```hlsl
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[shader("vertex")] VSOut vertexMain(uint vid : SV_VertexID) {
    float2 uv = float2((vid<<1)&2, vid&2);          // (0,0)(2,0)(0,2)
    VSOut o; o.uv = uv; o.pos = float4(uv*2-1, 0, 1); return o;
}
Texture2D<uint> gIndex : register(t0);              // R8_UINT 320×240
cbuffer Palette : register(b0) { uint gPal[32]; };  // RGBA8 packed
[shader("fragment")] float4 fragmentMain(VSOut i) : SV_Target {
    int2 p = int2(i.uv * float2(320,240));
    uint c = gPal[ gIndex.Load(int3(p,0)) ];
    return float4(c&0xff,(c>>8)&0xff,(c>>16)&0xff,255) / 255.0;
}
```

Adjust the Y-flip per VRI's standard top-left / Y-up clip convention (VRI normalizes across backends — verify against the triangle example).

**Location/build**: commit `source/lazy100/gpu/shaders/present.slang` together with the generated `present_spv.h` / `_wgsl.h` / `_dxbc.h` (same pattern as VRI's `tests/shaders/*_spv.h`). The kernel `#include`s the header and picks the blob by backend. Add a `task("shaders")` to Lazy-100's xmake that simply invokes VRI's already-built `vri-shaderc` (reuse, don't build another compiler):

```
vri-shaderc present.slang -o present_spv.h  --var g_presentSpv  --target spirv
vri-shaderc present.slang -o present_dxbc.h --var g_presentDxbc --target dxbc   (Windows host)
```

v1 minimum: SPIR-V (Vulkan + desktop GL); add DXBC if v1 includes a D3D12 target. Committing the headers means an ordinary build needs no Slang.

---

## 9. xmake Wiring

**Root `xmake.lua`**: change the template `PROJECT_NAME` token to `lazy100`; keep `add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")`, the mode rules, and the `includes("external"/"source"/"tests"/"examples")` order.

**Dependencies** (root or `external/xmake.lua`):
```lua
add_requires("libsdl3")
add_requires("miniaudio")
add_requires("lua 5.4")
add_requires("sol2")        -- header-only, compiled against the lua above
add_requires("vri")         -- published in the custom xmake-repo; no sibling-repo wiring needed
```

**`source/xmake.lua`**:
```lua
target("lazy100-static")
    set_kind("static")
    set_languages("cxx23")
    add_files("lazy100/**.cpp")
    add_includedirs("$(scriptdir)", {public = true})
    add_headerfiles("lazy100/**.hpp")
    add_packages("libsdl3", "miniaudio", "lua", "sol2", "vri", {public = true})
target_end()
```

**`examples/xmake.lua`**:
```lua
target("lazy100")
    set_kind("binary")
    add_files("run/main.cpp")
    add_deps("lazy100-static")
    add_packages("libsdl3")
    -- after_build copies examples/carts/*.lua next to the binary
target_end()
```

Note: the template already sets `/Zc:__cplusplus` + exceptions (required by sol2); debug builds enable VRI validation (`enableValidation = VRI_TRUE`) routed to `common/log` (`VriCallbackInterface`). Backend selection goes through VRI's `VriGraphicsAPI_Auto` + a `VRI_API` env-var override (on Windows the local default is Vulkan, with D3D12 optional).

---

## 10. Risks / Unknowns

1. **R8_UINT sampling support** — validate at boot with `GetFormatSupport`, keep the `R8_UNORM` fallback; fetch integer-texture values with `.Load`, never filter.
2. **Y-flip direction** — VRI normalizes top-left across backends; verify early with an asymmetric test sprite, otherwise some backend will be upside-down.
3. **Per-frame `Wait(fence)`** fully serializes CPU/GPU — fine for 320×240, but no overlap near vsync. If pacing is poor, move to N-deep frames-in-flight + staging/CBV rings. Not a v1 blocker.
4. **Swapchain resize/minimize** — handle `OutOfDate` on acquire (`swap.Resize`), skip rendering at 0 size.
5. **sol2 ⇄ Lua 5.4 coupling** — pin both versions; mind MSVC `/Zc:__cplusplus` + exceptions (already set).
6. **Lua error containment** — `sol::protected_function`; v1 policy: freeze and show the error on-screen vs exit.
7. **Audio scope creep** — v1 only inits miniaudio + `sfx()` pushing an id onto a lock-free queue, consumed by the audio callback (square-wave beep to validate the path). The queue boundary is the hook for a future synth/tracker. The tracker is not in v1.
8. **Sprite/palette data source** — v1 carts are pure `.lua` with no embedded sheet binary. Simplest: from Lua `sset()`, or a side-car indexed PNG loaded by `cart.cpp` (the `__gfx__` hex-string format is deferred).

---

## 11. References (Read-only)

- `../VRI/examples/common/example_app.h` — the standard VRI boot/present sequence
- `../VRI/examples/triangle/main.cpp` — minimal pipeline creation + per-backend blob selection
- `../VRI/include/vri/` — `vri_core.h` / `vri_command.h` / `ext/vri_ext_swapchain.h` / `integration/vri_sdl3.h`
