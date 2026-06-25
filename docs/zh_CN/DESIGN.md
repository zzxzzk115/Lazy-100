# Lazy-100 设计文档（运行时内核 / MVP v1）

> English version: [../DESIGN.md](../DESIGN.md)。

Lazy-100 是一台 **梦幻游戏主机（fantasy console）**，思路类似 PICO-8 / TIC-80 / basic8，但分辨率定为 **320×240**（128×128 太小，看不清字），脚本语言 **Lua 5.4**。它 **站在 VRI 的肩膀上**——VRI（`../VRI`）是一个跨后端的 RHI（Render Hardware Interface，抽象 Vulkan / D3D12 / WebGPU / GL）。

本文件描述 **运行时内核（v1）** 的架构。主机内编辑器（代码 / 精灵 / 地图 / 音乐 tracker）属于后续版本，不在此范围。

---

## 1. 技术栈

| 职责 | 选型 | xmake 包 | 说明 |
|------|------|----------|------|
| 渲染 | **VRI** | `vri` | 跨后端 RHI；自身不开窗，只接收原生句柄 |
| 开窗 + 输入 | **SDL3** | `libsdl3` | `<vri/integration/vri_sdl3.h>` 提供 `vriWindowHandleFromSDL3()` |
| 音频 | **miniaudio** | `miniaudio` | header-only，`MINIAUDIO_IMPLEMENTATION` 仅在一个 TU |
| 脚本绑定 | **sol2** | `sol2` | C++ ↔ Lua 绑定层 |
| 脚本运行时 | **Lua 5.4** | `lua` | 标准、可移植（含未来 WASM/Web 目标） |

> 用户初始设想 "VRI + miniaudio + sol2"。补齐缺口：sol2 只是绑定层，需 **lua** 本体；VRI 不开窗、不处理输入，需 **SDL3** 提供窗口句柄与键盘/手柄输入。

上述 5 个包在自定义 repo `https://github.com/zzxzzk115/xmake-repo.git` 中均已就绪——VRI 也在其中，故与其它依赖一样用 `add_requires("vri")` 直接引入。

---

## 2. 模块划分

分层原则：**video（纯 CPU 像素）→ gpu（唯一耦合 VRI）→ console（拥有循环）→ script（唯一耦合 sol2）**。
`video/` 不依赖 SDL/VRI/Lua，可 headless 单测、可 dump BMP；`present.cpp` 是唯一触碰 VRI 的文件，便于将来替换后端或加 SDL_Renderer 回退。

```
source/lazy100/
  common/
    types.hpp        固定宽度别名、Color32、Rect、小型数学、LZ_ASSERT
    log.hpp          日志；同时作为 VRI MessageCallback 的接收端
  video/             —— 确定性、可 headless 单测；无 SDL/VRI/Lua 依赖
    palette.*        32 项 RGBA8 调色板（默认 PICO-8 风格），set/get/reset
    framebuffer.*    320×240 uint8 索引缓冲 + clip 矩形 + camera 偏移
    draw.*           cls/pset/pget/line/rect/rectfill/circ/circfill/clip
    font.*           1bpp 位图字形表（ASCII 32..126），print() 光栅化
    sprites.*        精灵表（128×128 索引 = 16×16 个 8px 精灵），spr()/sspr()
    blit.hpp         内联 span/pixel 助手（clip + 透明索引处理）
  gpu/               —— 唯一触碰 VRI 的层
    present.*        VRI device+swapchain 持有者；上传索引纹理 + 全屏调色板解析
    shaders/
      present.slang  全屏三角 VS + 调色板解析 PS
      present_spv.h  生成物：g_presentSpv（Vulkan / 经 SPIRV-Cross 的 GL）
      present_dxbc.h 生成物（D3D12，Windows 宿主）
      present_wgsl.h 生成物（可选 v1）
  audio/
    audio.*          唯一定义 MINIAUDIO_IMPLEMENTATION；mixer + sfx() 无锁队列
  script/            —— 唯一 #include <sol/sol.hpp> 的层
    lua_api.*        sol2 把 draw/input/audio 绑定到 Console&
    lua_runtime.*    持有 sol::state；加载 cart；解析/调用 _init/_update/_draw
  cart/
    cart.*           加载 .lua（v1 = 纯脚本；嵌入式 sheet/palette 延后）
  input/
    input.*          6 键虚拟手柄；btn/btnp 位掩码；SDL3 keymap；逐步边沿
  console/
    console.*        编排者：持有 Window/Present/Framebuffer/Sprites/Input/Audio/Lua
    window.*         SDL3 开窗/销毁、事件泵、vriWindowHandleFromSDL3()
    config.hpp       LZ_W=320, LZ_H=240, 目标 fps, 调色板大小, 精灵表大小

examples/
  run/main.cpp       argv[1]=cart 路径；Console c; c.Boot(); c.Run();
  carts/             hello.lua, bounce.lua ...
tests/               可选 v1：video/ 的 host-only 单测（无 GPU）
external/xmake.lua   VRI 依赖接线
```

---

## 3. 帧循环（固定时间步）

Console 拥有主循环。逻辑用固定步累加器；render 跟随 vsync。

```text
Console::Run():
  lua.call_init()                      # cart _init() 调一次
  prev = clock::now(); acc = 0
  STEP = 1.0 / target_fps              # 默认 1/30；若 cart 定义 _update60 则 1/60
  while running:
      now = clock::now()
      frame_dt = min(now - prev, 0.25) # 防死亡螺旋
      prev = now; acc += frame_dt
      window.pump_events(running)      # SDL_PollEvent；快照原始键态进 Input
      while acc >= STEP:               # 固定步更新，0..N 次
          input.begin_step()           # pressed = held & ~prev_held
          lua.call_update()            # 游戏逻辑 + btn/btnp 读取
          input.end_step()             # prev_held = held
          acc -= STEP
      lua.call_draw()                  # cart 通过 draw API 写入 Framebuffer
      present.submit_frame(framebuffer, palette)
```

---

## 4. present.submit_frame —— VRI 序列

对齐 `VRI/examples/common/example_app.h` 的 acquire → barrier → upload → 全屏 draw → present → fence-wait。

```text
present.submit_frame(fb, pal):
  if pal.dirty: map paletteBuf; memcpy 32×RGBA8; unmap; dirty=false
  memcpy(MapBuffer(stagingRing[frame%N]), fb.pixels(), 320*240); UnmapBuffer

  AcquireNextTexture(swapchain, &index)        # 处理 OutOfDate → swap.Resize 并跳过
  bbView = CreateTextureView(backbuffers[index])

  BeginCommandBuffer(cmd)
    CmdBarrier(indexTex: * -> CopyDestination)
    CmdUploadBufferToTexture(indexTex <- staging)   # 紧密打包 320×240 R8；VRI 处理 row pitch
    CmdBarrier(indexTex: CopyDestination -> ShaderResource @ FragmentShader)
    CmdBarrier(backbuffer: Undefined -> ColorAttachment)
    CmdBeginRendering({color=bbView, loadOp=Clear(黑边)})
      CmdSetViewports(整数缩放 letterbox 矩形); CmdSetScissors(全窗口)
      CmdSetPipelineLayout(presentLayout); CmdSetPipeline(presentPipeline)
      CmdSetDescriptorSet(0, {indexTex SRV, palette CBV, NEAREST sampler})
      CmdDraw({vertexNum=3})                          # 全屏三角，无顶点缓冲
    CmdEndRendering()
    CmdBarrier(backbuffer: ColorAttachment -> Present)
  EndCommandBuffer(cmd)
  QueueSubmit(queue, {cmd, signal fence=++frameValue})
  Wait(fence, frameValue)                             # v1: 简单 CPU-GPU 同步（见风险 4）
  Present(swapchain)
  DestroyDescriptor(bbView)
```

保持一个 N=image-count 的 staging **环形缓冲**，避免逐帧重分配。

---

## 5. 像素格式：上传 R8_UINT 索引纹理 + 片元着色器查调色板

**结论：采用方案 (b)** —— 上传 `R8_UINT` 索引纹理，在片元着色器里用 32 项调色板 uniform 解析颜色。**不**在 CPU 端逐帧展开成 RGBA8。

- 索引纹理：`VriTextureDesc{ 2D, R8_UINT, 320×240, usage=ShaderResource|copy-dst, Device }`。CPU framebuffer 正好是 `uint8_t[320*240]`（75 KB），memcpy 进 HostUpload staging，再 `CmdUploadBufferToTexture`。
- 调色板：32 项作为 **constant buffer**（`uint32 palette[32]` RGBA8 打包），CBV 绑定，仅 `dirty` 时更新（通常零成本）。
- 片元着色器：`Texture2D<uint>` 用 `.Load(int3(p,0))` texelFetch（整数纹理不可线性过滤，正好配最近邻放大），`palette[idx]` 输出 RGBA。

**理由（对比方案 a：CPU 逐帧展开 index→RGBA8）**
- **带宽**：75 KB/帧 vs 300 KB/帧——省 4×。
- **调色板特效几乎免费**：`pal()` 替换、调色板循环、淡入淡出、闪屏 = 改 32 项 uniform，而非重写 76800 像素。这正是 fantasy console 的惯用语，也是方案 (a) 的痛点。
- **CPU 保持廉价**：光栅器永远只写 1 字节/像素。

**已处理的坑**：boot 时 `GetFormatSupport(R8_UINT)` 校验；若某后端不支持采样整数纹理，回退 `R8_UNORM` + `round(s*255)`，改动隔离在 `present.slang` + 一个格式常量。

---

## 6. Lua API（v1）

全局自由函数（PICO-8 惯例：cart 调 `pset(...)` 而非 `lz.pset(...)`）。颜色 = 调色板索引 `0..31`。

```text
-- 生命周期（cart 定义，内核调用）
_init()   _update() / _update60()   _draw()

-- 图形（写索引 framebuffer）
cls([c])  pset(x,y,[c])  pget(x,y)->c
line(x0,y0,x1,y1,[c])  rect(x0,y0,x1,y1,[c])  rectfill(...)  circ(x,y,r,[c])  circfill(...)
clip([x,y,w,h])  camera([dx,dy])
pal([c0,c1])    -- 绘制色重映射；pal() 重置
palt([c,t])     -- 索引 c 透明（默认索引 0 透明）

-- 文本
print(text,[x],[y],[c])->nextx    cursor(x,y,[c])

-- 精灵
spr(n,x,y,[w=1],[h=1],[flip_x],[flip_y])
sspr(sx,sy,sw,sh,dx,dy,[dw],[dh],[fx],[fy])
sget(x,y)->c   sset(x,y,[c])   fget/fset（v1 stub：精灵标志）

-- 输入
btn([i],[p=0])->bool    -- i ∈ 0..5（L,R,U,D,O/Z,X）；无参 -> 位掩码
btnp([i],[p=0])->bool   -- 按下那一步的边沿，带 auto-repeat

-- 音频（v1 stub 路径）
sfx(n,[chan],[off])     -- 路由到 audio.trigger_sfx(n)
music(n)                -- v1 no-op

-- 数学/工具
flr ceil abs min max mid sgn sqrt sin cos atan2 rnd srand t
```

**sol2 绑定**（`lua_api.cpp`，薄 trampoline 到 video/input）：

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

`lua_runtime.cpp` 把回调缓存为 `sol::protected_function`，cart 出错时打印诊断并冻结该 cart，而非崩溃宿主：

```cpp
sol::protected_function cb_update = L["_update"];   // 可能为 nil
// 每步: if (cb_update.valid()) { auto r = cb_update(); if(!r.valid()) report_lua_error(r); }
```

---

## 7. 固定时间步 & 输入模型

**FPS 决策：逻辑固定步默认 30 Hz，present 跟随 vsync。** PICO-8 同时提供 `_update()`@30 与 `_update60()`@60。固定 30 Hz 是经典手感，CPU 减半，且让 `btnp` auto-repeat 时序符合预期。若 cart 定义 `_update60` 则 STEP=1/60。逻辑与 present 解耦很重要，因为 present 锁 vsync 而显示器可能 60/120/144 Hz。

**输入（`input/input.cpp`）** —— 六个虚拟键，v1 仅 player 0：
- `held_mask`：每次泵从 SDL3 键盘态重建（方向键 + Z/X，keymap 可配），`btn` 实时读。
- `prev_mask`：上一 **逻辑步** 时的 held 掩码。
- `pressed_mask = held_mask & ~prev_mask` → `btnp`，在 `begin_step()` 计算，**按逻辑步采样而非渲染帧**。关键：60 Hz render + 30 Hz logic 时，一帧轻点必须恰好触发一次，且一渲染帧内 N 次追赶步各自看到正确的按下语义。
- auto-repeat（PICO-8：15 帧延迟后每 4 帧）：每键维护 held-step 计数器，在对应间隔发 repeat 脉冲。

---

## 8. 着色器处理

VRI 消费 **预编译** 字节码（Slang → SPIR-V/WGSL/DXBC 离线，见 VRI 的 `tools/vri-shaderc` 与 `xmake/tasks/shaders.lua`）。Lazy-100 照搬此模式。

**v1 仅需一个程序**：`present.slang`——全屏三角 VS（从 `SV_VertexID` 生成 3 个裁剪顶点，无顶点缓冲）+ 调色板解析 PS：

```hlsl
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
[shader("vertex")] VSOut vertexMain(uint vid : SV_VertexID) {
    float2 uv = float2((vid<<1)&2, vid&2);          // (0,0)(2,0)(0,2)
    VSOut o; o.uv = uv; o.pos = float4(uv*2-1, 0, 1); return o;
}
Texture2D<uint> gIndex : register(t0);              // R8_UINT 320×240
cbuffer Palette : register(b0) { uint gPal[32]; };  // RGBA8 打包
[shader("fragment")] float4 fragmentMain(VSOut i) : SV_Target {
    int2 p = int2(i.uv * float2(320,240));
    uint c = gPal[ gIndex.Load(int3(p,0)) ];
    return float4(c&0xff,(c>>8)&0xff,(c>>16)&0xff,255) / 255.0;
}
```

按 VRI 标准 top-left / Y-up 裁剪约定调整 Y 翻转（VRI 跨后端归一化——对照 triangle 示例验证）。

**位置/构建**：`source/lazy100/gpu/shaders/present.slang` + 生成的 `present_spv.h` / `_wgsl.h` / `_dxbc.h` 一并提交（同 VRI 的 `tests/shaders/*_spv.h` 模式）。内核 `#include` 头文件并按后端选 blob。给 Lazy-100 的 xmake 加一个 `task("shaders")`，直接调用 VRI 已构建的 `vri-shaderc`（复用，不另造编译器）：

```
vri-shaderc present.slang -o present_spv.h  --var g_presentSpv  --target spirv
vri-shaderc present.slang -o present_dxbc.h --var g_presentDxbc --target dxbc   (Windows 宿主)
```

v1 最低：SPIR-V（Vulkan + 桌面 GL）；若 v1 含 D3D12 目标再加 DXBC。提交头文件意味着普通构建无需 Slang。

---

## 9. xmake 接线

**根 `xmake.lua`**：把模板 `PROJECT_NAME` token 改成 `lazy100`；保留 `add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")`、mode 规则、`includes("external"/"source"/"tests"/"examples")` 顺序。

**依赖**（根或 `external/xmake.lua`）：
```lua
add_requires("libsdl3")
add_requires("miniaudio")
add_requires("lua 5.4")
add_requires("sol2")        -- header-only，对上面的 lua 编译
add_requires("vri")         -- 已发布于自定义 xmake-repo，无需兄弟仓库接线
```

**`source/xmake.lua`**：
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

**`examples/xmake.lua`**：
```lua
target("lazy100")
    set_kind("binary")
    add_files("run/main.cpp")
    add_deps("lazy100-static")
    add_packages("libsdl3")
    -- after_build 把 examples/carts/*.lua 拷到二进制旁
target_end()
```

注意：模板已设 `/Zc:__cplusplus` + 异常（sol2 需要）；debug 开 VRI validation（`enableValidation = VRI_TRUE`）路由到 `common/log`（`VriCallbackInterface`）。后端选择走 VRI 的 `VriGraphicsAPI_Auto` + `VRI_API` 环境变量覆盖（本机 Windows 默认 Vulkan，可选 D3D12）。

---

## 10. 风险 / 未知

1. **R8_UINT 采样支持**——boot 用 `GetFormatSupport` 校验，保留 `R8_UNORM` 回退；整数纹理用 `.Load` 取值，绝不过滤。
2. **Y 翻转方向**——VRI 跨后端归一化 top-left；用非对称测试精灵早验，否则某后端会上下颠倒。
3. **逐帧 `Wait(fence)`** 完全序列化 CPU/GPU——320×240 够用，但接近 vsync 无重叠。若 pacing 不佳再上 N-deep frames-in-flight + staging/CBV 环。非 v1 阻塞项。
4. **swapchain resize/minimize**——处理 acquire 的 `OutOfDate`（`swap.Resize`），0 尺寸跳过渲染。
5. **sol2 ⇄ Lua 5.4 耦合**——两者版本钉死；注意 MSVC `/Zc:__cplusplus` + 异常（已设）。
6. **Lua 错误容器**——`sol::protected_function`；v1 策略：冻结并屏显错误 vs 退出。
7. **音频范围蔓延**——v1 仅 init miniaudio + `sfx()` 往无锁队列推 id，音频回调消费（方波 beep 验证路径）。队列边界是未来合成器/tracker 的接入点。tracker 不进 v1。
8. **精灵/调色板数据来源**——v1 cart 为纯 `.lua` 无嵌入式 sheet 二进制。最简：从 Lua `sset()`，或旁挂索引 PNG 由 `cart.cpp` 加载（`__gfx__` 十六进制串格式延后）。

---

## 11. 参考（只读）

- `../VRI/examples/common/example_app.h` —— 标准 VRI boot/present 序列
- `../VRI/examples/triangle/main.cpp` —— 最小 pipeline 创建 + 按后端选 blob
- `../VRI/include/vri/` —— `vri_core.h` / `vri_command.h` / `ext/vri_ext_swapchain.h` / `integration/vri_sdl3.h`
