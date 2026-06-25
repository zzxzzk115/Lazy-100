# Lazy-100 TODO（运行时内核 / MVP v1）

> English version: [../TODO.md](../TODO.md)。

垂直切片里程碑——**每个里程碑结束都能 `xmake && xmake run lazy100 examples/carts/<cart>.lua` 跑通**。
M0/M1 在搭 Lua/玩法层之前先消除 VRI 集成与 GPU 风险。

详见 [DESIGN.md](DESIGN.md)。

---

## M0 — 黑窗（boot/present 主干）

- [ ] 根 `xmake.lua`：`PROJECT_NAME` → `lazy100`，更新 README
- [ ] `add_requires` vri / libsdl3 / miniaudio / lua 5.4 / sol2（VRI 现为自定义 repo 包，直接 `add_requires("vri")`）
- [ ] `console/window.*`：SDL3 开窗 + 事件泵 + `vriWindowHandleFromSDL3()`
- [ ] `gpu/present.*`：VRI device + swapchain 创建；debug validation 路由到 `common/log`
- [ ] 每帧 acquire → clear backbuffer → present；处理 `OutOfDate`/resize/minimize
- [ ] `examples/run/main.cpp`：`Console c; c.Boot(); c.Run();`
- **验收**：弹出窗口，稳定显示纯色清屏，可关闭

## M1 — Framebuffer 上屏（消除 GPU 风险）

- [ ] `video/palette.*`：32 项 RGBA8 默认调色板
- [ ] `video/framebuffer.*`：320×240 uint8 索引缓冲 + `cls/pset/rectfill`
- [ ] `gpu/shaders/present.slang`：全屏三角 VS + 调色板解析 PS；用 `vri-shaderc` 生成 `present_spv.h`（+ 按需 `_dxbc.h`）
- [ ] `present.*`：R8_UINT 索引纹理 + staging 环上传 + 调色板 CBV + NEAREST sampler + 全屏 quad
- [ ] boot 时 `GetFormatSupport(R8_UINT)` 校验（含 R8_UNORM 回退）
- [ ] 整数缩放 letterbox viewport
- [ ] C++ 硬编码测试图案 + 非对称精灵验 Y 翻转方向
- **验收**：320×240 测试图案正确放大上屏，方向正确，无模糊

## M2 — Lua 绘图

- [ ] `script/lua_runtime.*`：sol::state，加载 `.lua` cart，解析 `_init`/`_draw`（`sol::protected_function` 容错）
- [ ] `video/draw.*`：line/rect/rectfill/circ/circfill/clip/camera
- [ ] `video/font.*`：1bpp 位图字体 + `print`
- [ ] `script/lua_api.*`：绑定 cls/pset/pget/line/rect/rectfill/circ/circfill/clip/camera/print + 数学（flr/min/max/rnd/sin/cos...）
- [ ] `cart/cart.*`：从磁盘加载纯 `.lua`
- [ ] `examples/carts/hello.lua`
- **验收**：cart 用 Lua 画形状与文字并显示

## M3 — 循环 + 输入

- [ ] `console/console.*`：固定时间步累加器（30Hz，`_update60` → 60Hz），`min(dt,0.25)` 钳制
- [ ] `input/input.*`：6 键虚拟手柄，SDL3 keymap，`held/prev/pressed` 掩码，逐步边沿，auto-repeat（15 后每 4）
- [ ] 绑定 `btn`/`btnp`，接 `_update`
- [ ] `examples/carts/bounce.lua`（可交互移动光点）
- **验收**：方向键/Z/X 实时响应，btnp 单帧轻点恰好一次

## M4 — 精灵 + 字体

- [ ] `video/sprites.*`：128×128 索引精灵表 + `spr`/`sspr`/`sget`/`sset`，flip
- [ ] `pal`/`palt` 透明（默认索引 0 透明）
- [ ] 绑定 spr/sspr/sget/sset/fget/fset/pal/palt
- [ ] 精灵数据来源：sset() 或旁挂索引 PNG 加载
- [ ] `examples/carts/sprite.lua`
- **验收**：精灵带透明正确绘制，翻转/调色板重映射生效

## M5 — 音频接缝

- [ ] `audio/audio.*`：miniaudio device + 回调（`MINIAUDIO_IMPLEMENTATION` 仅此 TU）
- [ ] `sfx()` 无锁队列：Lua 推 id → 回调消费 → 方波 beep
- [ ] 绑定 `sfx`/`music`（music 为 no-op stub）
- **验收**：cart 调 `sfx(0)` 听到 beep；队列边界为未来 tracker 接入点

---

## 后续版本（不在 v1）
- 主机内编辑器：代码编辑器、精灵编辑器、地图编辑器、音乐 tracker
- cart 打包格式（`__lua__`/`__gfx__`/`__sfx__` 段）
- 完整音频合成器 / tracker
- N-deep frames-in-flight 渲染优化
- WASM/Web 目标
