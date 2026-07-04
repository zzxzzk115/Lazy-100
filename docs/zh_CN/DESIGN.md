# Lazy-100 设计文档

> English version: [../DESIGN.md](../DESIGN.md)

Lazy-100 是一台向 PICO-8 / TIC-80 / basic8 致敬的**幻想游戏机**,分辨率固定为
**320×240**(文字可读),256 色调色板,4 通道芯片音频,卡带用 **Lua 5.4** 编写。它**站在
VRI 的肩膀上**(`../VRI`)——一个跨后端 RHI(Vulkan / D3D12 / Metal / WebGPU / GL /
GLES / WebGL)。

主机已完整且自举:所有功能——shell、代码 / 精灵 / 地图 / 音效 / 音乐编辑器、在线卡带
浏览器——都在主机内部运行,支持桌面(Windows / Linux / macOS)与网页(Emscripten,
[zzxzzk115.github.io/Lazy-100](https://zzxzzk115.github.io/Lazy-100/))。它还能通过内置的
定点 **z8lua** 虚拟机原生运行 p8 卡带。本文描述架构;面向卡带的 API 见
[CHEATSHEET.md](CHEATSHEET.md)。

---

## 1. 技术栈

| 职责 | 选型 | xmake 包 | 说明 |
|------|------|----------|------|
| 渲染 | **VRI** | `vri` | 跨后端 RHI;自身不开窗,只接收原生句柄 |
| 窗口 + 输入 | **SDL3** | `libsdl3` | `<vri/integration/vri_sdl3.h>` 提供 `vriWindowHandleFromSDL3()` |
| 音频 | **miniaudio** | `miniaudio` | 头文件库;`MINIAUDIO_IMPLEMENTATION` 只在 audio.cpp 一个编译单元 |
| 脚本绑定 | **sol2** | `sol2` | C++ ↔ Lua 绑定层(原生卡带) |
| 脚本运行时 | **Lua 5.4** | `lua` | 原生卡带;可移植(含 WASM 目标) |
| p8 运行时 | **z8lua**(内置) | — | 定点 Lua 分支,带 p8 方言;`external/z8lua` |

所有外部包都来自自定义仓库 `https://github.com/zzxzzk115/xmake-repo.git`,VRI 与普通依赖
一样 `add_requires("vri")` 即可。

---

## 2. 模块布局

分层原则:**video(纯 CPU 像素)→ gpu(唯一耦合 VRI 的层)→ console(拥有主循环)→
script(唯一耦合虚拟机的层)**。`video/` 不依赖 SDL/VRI/Lua,因此无头工具(`cartshot`、
`cartwav`)和测试可以直接链接。

```
source/lazy100/
  common/          类型、日志、letterbox 布局数学
  video/           调色板、320×240 索引帧缓冲、绘制、字体(TIC-80 拉丁位图 +
                   缝合像素 8px CJK,stb_truetype)、精灵、像素光标、图标
  gpu/             present.* —— 唯一的 VRI 代码:索引纹理上传 + 调色板解析
  audio/           4 通道合成器:sfx 音型 + 歌曲音序器、扬声器预热、
                   网页后台设备销毁/重建;离线 WAV 渲染
  script/          lua_runtime.*(sol2,原生卡带)· p8_vm.*(z8lua,p8 卡带)·
                   lua_api.*(共享 C++ API 面)—— 按卡带语言双 VM 路由
  cart/            .lz100 文本格式(代码+图形+旗标+地图+音效+音乐+标签+标题/作者),
                   cartpng.* —— 可分享的 .lz100.png 卡带(载荷藏于低 2 位)
  input/           input.* 6 键手柄 · keyboard.* 全键盘 · mouse.* —— 三者都接受
                   网页注入(触屏手柄 / 屏幕键盘 / 画布触控板)
  world/           128×64 瓦片地图
  shell/           命令行(help/ls/cd/load/save/run/title/author/…)
  editor/          编辑器宿主 + 代码 / 精灵 / 地图 / 音效 / 音乐编辑器,共享 ui/图标
  explore/         Lazy-100-games 目录的在线卡带浏览器
  net/             fetch(原生 curl / emscripten_fetch)
  vfs/             内嵌资源(字体)+ 持久化存档(网页用 IDBFS)
  console/         调度器:模式状态机、开机仪式、暂停菜单、love2d 式错误屏、
                   卡带生命周期;window.*(SDL3)

source/lazy100-app/  宿主程序 + 网站驱动的 wasm C 导出
tools/               cartshot(无头首帧 PNG)· cartwav(无头音乐 WAV)
web/site/            两页站点(home = 完整主机,carts = 目录网格)
external/z8lua/      内置定点 Lua,用于 p8 卡带
```

---

## 3. 主机模式与主循环

主机是固定步长循环驱动的模式状态机(逻辑 30 Hz,卡带定义 `_update60` 则 60 Hz;
present 跟随垂直同步):

```
Boot ──开机动画──▶ Shell ◀──ESC──▶ Editor ◀─┐
                    │  ▲                    │ 暂停菜单
                    ▼  │                    │ (continue / reset / edit / explore / shell)
                 Explore ────▶ Running ◀────┘
```

- **Boot**:开机动画 + 音频预热;网页上兼作 press-any-key 门(解锁 AudioContext),
  可先*装填*卡带,由门的手势直接启动。
- **Running**:固定步长 `_update`/`_update60` + `_draw`;ESC 打开暂停菜单。脚本出错时
  卡带冻结在蓝色错误屏(核心错误信息 + "press ESC to edit and fix it");代码编辑器的
  内联错误条显示同一条信息,直到下次干净运行。
- **Editor**:多标签套件(代码/精灵/地图/音效/音乐),Ctrl+Tab 轮换。
- 加载时检测卡带语言:`.lz100`/`.lua` → sol2 + Lua 5.4;p8 卡带 → z8lua 真定点方言
  (非转译)。

---

## 4. Present:R8_UINT 索引纹理 + 调色板解析

CPU 帧缓冲是 `uint8[320*240]`。每帧 memcpy 进暂存环并作为 `R8_UINT` 纹理上传;全屏
三角形的片元着色器通过 256 项调色板常量缓冲(仅脏时更新)解析颜色。整数纹理用 `.Load`
取样——不滤波,恰好配合整数倍最近邻放大 + 居中 letterbox(黑边由渲染通道清出)。

不在 CPU 上展开成 RGBA 的原因:上传带宽 4 倍,而且调色板特效(`pal()` 置换、渐隐、
循环)本应只改 256 个 uniform,而不是重写 76800 个像素。

交换链每帧跟踪窗口(`drawable_size`)自动重建;网页上画布必须**经由 SDL** 调整尺寸
(`lazy100_resize` → `SDL_SetWindowSize`),保证 SDL、画布后备存储与 letterbox 数学
永不失配。

着色器离线预编译(Slang → SPIR-V / DXBC / WGSL 头文件,用 VRI 的 `vri-shaderc` 生成)
并提交进仓库,普通构建无需着色器工具链。

---

## 5. 卡带格式

- **`.lz100`** —— 单个纯文本文件,分节(`__lua__`、`__gfx__`、`__gff__`、`__map__`、
  `__sfx__`、`__music__`、`__label__`),外加 `title` / `author` 头行。编辑器产出的一切
  都经它往返。
- **`.lz100.png`** —— 可分享卡带:渲染出的卡带图(头带、固定偏移的 320×240 截图、
  标题/作者脚注),RLE 压缩的 `.lz100` 文本藏在 RGBA 像素低 2 位。图片本身兼作目录预览。
- **p8 卡带** —— 文本与 PNG 形式走同一管线,路由到 z8lua。p8 是兼容特性;公开目录只收
  `.lz100.png` 卡带。

---

## 6. 输入

三个输入面,都有网页注入通道(触屏站点经 C 导出驱动):

| 输入面 | 原生来源 | 网页注入 |
|---|---|---|
| `Input`(6 键手柄) | SDL 扫描码(方向键 + Z/X/C/V) | `lazy100_set_pad` 掩码 |
| `Keyboard`(编辑器/shell) | SDL 键状态 + 文本输入 | `lazy100_set_keys` 掩码 + `lazy100_type_text` |
| `Mouse`(编辑器) | SDL 指针经 letterbox 变换 | `lazy100_set_mouse`(画布即触控板) |

手柄边沿(`btnp`)按**逻辑步**采样(而非渲染帧),经典 15/4 帧自动重复,任何刷新率下
点按手感一致。

---

## 7. 音频

miniaudio 渲染回调里的 4 通道芯片合成器:64 个 sfx 音型(32 步 × 音高 / 波形 / 音量 /
效果)+ 64 行歌曲音序器;sfx 声部按通道遮蔽音乐声部,音乐在下面静默继续走步,sfx 结束
即同步恢复。设备启动后约 1.5 秒次听阈抖动唤醒省电扬声器。

网页特有:设备在首个用户手势栈内启动(解锁 AudioContext);页面切后台时站点自动暂停
卡带并**销毁设备**(`lazy100_audio_suspend`),回前台重建全新设备——恢复被 iOS 挂起的
旧 AudioContext 不可靠,新建一个才可靠。`cartwav` 可无头渲染音序器到 WAV 做分析。

---

## 8. 网站与导出

`web/site/` 是两页静态站(home = 完整主机;`/carts/` = 目录网格,点击回跳
`/?cart=<id>`),推送 `doc` 分支即部署(GitHub Actions 用 `scripts/build_site.sh` 构建
wasm 主机)。宿主程序导出站点所需的 C 钩子:卡带启动/装填、手柄/键盘/文本/鼠标注入、
模式查询(切换触屏控件组)、画布缩放、后台暂停、音频挂起/恢复/预热。移动端跑卡带时
显示虚拟手柄,shell/编辑器下显示屏幕键盘 + 画布触控板。

---

## 9. 构建与工具

全程 xmake。`xmake` 构建宿主;`xmake f -p wasm && xmake build lazy100` 构建网页主机;
`scripts/build_site.sh` 把整个站点装配到 `build/site/`。宿主工具:`cartshot`(无头首帧
`.png` / 打包 `.lz100.png`)与 `cartwav`。测试在 `tests/`(`lazy100_build_tests=n` 可
跳过)。Windows 使用静态 CRT(MT)与 VRI 对齐。CI:`deploy_pages.yaml`(站点,`doc`
推送触发)与 `release_prebuilt.yaml`(发版时打包各平台主机 + 工具 zip)。
