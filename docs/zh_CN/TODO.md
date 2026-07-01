# Lazy-100 TODO（运行时内核 + 编辑器套件 V1）

> English version: [../TODO.md](../TODO.md)。

垂直切片里程碑——**每个里程碑结束都能 `xmake && xmake run lazy100 carts/<cart>.lua` 跑通**（或 `xmake run lazy100` 无参进入命令行）。
M0/M1 在搭 Lua/玩法层之前先消除 VRI 集成与 GPU 风险；M6 起加入主机内编辑器。

详见 [DESIGN.md](DESIGN.md)。

---

## M0 — 黑窗（boot/present 主干）✅

- [x] 根 `xmake.lua`：`PROJECT_NAME` → `lazy100`，更新 README
- [x] `add_requires` vri / libsdl3 / miniaudio / lua 5.4 / sol2（VRI 现为自定义 repo 包，直接 `add_requires("vri")`）
- [x] `console/window.*`：SDL3 开窗 + 事件泵 + `vriWindowHandleFromSDL3()`
- [x] `gpu/present.*`：VRI device + swapchain 创建；debug validation 路由到 `common/log`
- [x] 每帧 acquire → clear backbuffer → present；处理 `OutOfDate`/resize/minimize
- [x] `examples/run/main.cpp`：`Console c; c.Boot(); c.Run();`
- **验收**：弹出窗口，稳定显示纯色清屏，可关闭

## M1 — Framebuffer 上屏（消除 GPU 风险）✅

- [x] `video/palette.*`：256 项 RGBA8 默认调色板（32 精选 + 6×6×6 色立方 + 灰阶）
- [x] `video/framebuffer.*`：320×240 uint8 索引缓冲 + `cls/pset/rectfill`
- [x] `gpu/shaders/present.slang`：全屏三角 VS + 调色板解析 PS；用 `vri-shaderc` 生成 `present_spv.h`（+ 按需 `_dxbc.h`）
- [x] `present.*`：R8_UINT 索引纹理 + staging 环上传 + 调色板 CBV + NEAREST sampler + 全屏 quad
- [x] boot 时 `GetFormatSupport(R8_UINT)` 校验（含 R8_UNORM 回退）
- [x] 整数缩放 letterbox viewport
- [x] C++ 硬编码测试图案 + 非对称精灵验 Y 翻转方向
- **验收**：320×240 测试图案正确放大上屏，方向正确，无模糊

## M2 — Lua 绘图 ✅

- [x] `script/lua_runtime.*`：sol::state，加载 `.lua` cart，解析 `_init`/`_draw`（`sol::protected_function` 容错）
- [x] `video/draw.*`：line/rect/rectfill/circ/circfill/clip/camera
- [x] `video/font.*`：1bpp 位图字体 + `print`
- [x] `script/lua_api.*`：绑定 cls/pset/pget/line/rect/rectfill/circ/circfill/clip/camera/print + 数学（flr/min/max/rnd/sin/cos...）
- [x] `cart/cart.*`：从磁盘加载纯 `.lua`
- [x] `examples/carts/hello.lua`
- **验收**：cart 用 Lua 画形状与文字并显示

## M3 — 循环 + 输入 ✅

- [x] `console/console.*`：固定时间步累加器（30Hz，`_update60` → 60Hz），`min(dt,0.25)` 钳制
- [x] `input/input.*`：6 键虚拟手柄，SDL3 keymap，`held/prev/pressed` 掩码，逐步边沿，auto-repeat（15 后每 4）
- [x] 绑定 `btn`/`btnp`，接 `_update`
- [x] `examples/carts/bounce.lua`（可交互移动光点）
- **验收**：方向键/Z/X 实时响应，btnp 单帧轻点恰好一次

## M4 — 精灵 + 字体 ✅

- [x] `video/sprites.*`：256×256 索引精灵表（16×16 个 16px 精灵）+ `spr`/`sspr`/`sget`/`sset`，flip
- [x] `pal`/`palt` 透明（默认索引 0 透明）
- [x] 绑定 spr/sspr/sget/sset/fget/fset/pal/palt
- [x] 精灵数据来源：sset() 或旁挂索引 PNG 加载
- [x] `examples/carts/sprite.lua`
- **验收**：精灵带透明正确绘制，翻转/调色板重映射生效

## M5 — 音频接缝 ✅

- [x] `audio/audio.*`：miniaudio device + 回调（`MINIAUDIO_IMPLEMENTATION` 仅此 TU）
- [x] `sfx()` 无锁队列：Lua 推 id → 回调消费 → 方波 beep
- [x] 绑定 `sfx`/`music`（music 为 no-op stub）
- **验收**：cart 调 `sfx(0)` 听到 beep；队列边界为未来 tracker 接入点

---

# 编辑器套件 V1（主机内创作 + cart 格式）

运行时内核已能*运行* cart；V1 让主机*自包含*——在主机内编写代码、绘制精灵、拼地图、谱曲，
存/取单个可分享的 `.lz100`。

## M6 — 命令行 + mode 系统 + 完整输入 ✅

- [x] `console/window.*`：采集文本输入（`SDL_EVENT_TEXT_INPUT`）+ 鼠标 motion/button/wheel
- [x] `input/keyboard.*`：全键盘边沿 + auto-repeat + 输入的 UTF-8（区别于游戏 `Input`）
- [x] `input/mouse.*`：鼠标经 letterbox 映射到 320×240 framebuffer + 按键边沿
- [x] `console/console.*`：`ConsoleMode {Shell, Running, Editor}` 分派；ESC 切换
- [x] `shell/shell.*`：类 Linux 命令行（cd/pwd/ls/help/Tab 补全/历史），沙箱到 `carts/`
- [x] `editor/editor.*`：`Editor` 接口 + `EditorHost` tab 栏
- **验收**：启动进命令行、打字/`ls`；ESC 进带 tab 的编辑器 ✅

## M7 — Cart 模型 + `.lz100` 加载/保存 ✅

- [x] `cart/cart.*`：PICO-8 风单文本文件，分段 parse/serialize
- [x] 从 `Console::code()` 运行（非文件）；加载时还原精灵表
- [x] 命令行 `load`/`save`/`run`/`new`；资产经 `.rc`/`.S` link 进二进制 + 内存 VFS
- **验收**：`load demo.lz100` → `run`；`save` 往返不丢代码+精灵 ✅

## M8 — 精灵编辑器 ✅

- [x] `editor/sprite_editor.*`：放大画布、调色板网格、精灵表导航、取色
- **验收**：画一个精灵 → `run` 一张 `spr()` 它的 cart；随 `__gfx__` 存取 ✅

## 规格升级 — 比 PICO-8 更高一档 ✅

- [x] 调色板 32 → **256** 色；精灵 8×8 → **16×16**；精灵表 128×128 → **256×256**
- [x] 贯穿默认调色板、present 着色器（`uint4[64]`）、精灵、cart `__gfx__`、精灵编辑器

## M9 — 地图 + 地图运行时 ✅

- [x] `world/map.*`：128×64 tile；Lua `mget`/`mset`/`map(cx,cy,sx,sy,cw,ch)`（16px tile）
- [x] `editor/map_editor.*`：滚动视口（刷/擦、方向键平移）、精灵选择器、tile 预览
- [x] cart `__map__` 段
- **验收**：刷地图 → cart `map()` 渲染；随 cart 存取 ✅

## M10 — 代码编辑器 ✅

- [x] `editor/code_editor.*`：行缓冲、光标/滚动/插入/删除/换行/Tab、行号栏、光标
- [x] UTF-8 感知（光标按完整码点移动——中日韩正确编辑）；与 `Console::code()` 同步
- **验收**：主机内改 Lua → ESC → 命令行 `run` 生效 ✅

## M11 — 音乐/音效 tracker + 音频合成升级 ✅

- [x] `audio/sound.*`：`SfxPattern`（32 步：音高/波形/音量/效果 + 速度）、`MusicPattern`（4 通道）、`SoundBank`
- [x] 音频引擎：5 波形（方波/脉冲/三角/锯齿/噪声）、无爆音包络、半音音高、4 通道 + 音乐音序器
- [x] 线程安全交接：sfx 经 SPSC 队列快照；music 经原子翻转的 SoundBank 快照
- [x] cart `__sfx__`/`__music__`；Lua `sfx(n[,chan])` / `music(n)` / `music(-1)`
- [x] `editor/sfx_editor.*`（钢琴卷帘 + 波形/音量/速度选择 + 试听）、`editor/music_editor.*`（4 通道编排 + 走带）
- **验收**：编一条 sfx → 试听/`sfx(0)`；编一段 music → `music(0)`；随 cart 存取 ✅

---

## V1 之后（尚未）
- 代码编辑器语法高亮 + 横向滚动；等宽代码字形
- 保留槽位之外的 sfx 效果（滑音/颤音/淡入淡出）
- 精灵/地图/tracker 的复制粘贴；撤销
- N-deep frames-in-flight 渲染优化
- WASM/Web 目标
