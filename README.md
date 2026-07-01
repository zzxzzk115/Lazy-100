# Lazy-100

A **fantasy game console** in the spirit of PICO-8 / TIC-80 / basic8, but a notch more
capable: a readable **320×240** screen, a **256-color** palette, **16×16** sprites, and
scripting in **Lua 5.4**. It stands on the shoulders of
[VRI](https://github.com/zzxzzk115/VRI), a cross-backend Render Hardware Interface
(Vulkan / D3D12 / Metal / WebGPU / OpenGL / OpenGL ES / WebGL).

## Specs

| | |
|---|---|
| Screen | 320×240, indexed (1 byte/pixel) |
| Palette | 256 colors (fully re-definable) |
| Sprites | 16×16 px, 16×16 sheet = 256 sprites (256×256 px) |
| Map | 128×64 tiles |
| Script | Lua 5.4 (sol2) |
| Cart | single `.lz100` text file (code + gfx + flags + map + audio) |

## Stack

- **VRI** — rendering (Vulkan backend by default)
- **SDL3** — window + keyboard/gamepad input
- **miniaudio** — audio output
- **Lua 5.4 + sol2** — cart scripting

## Build

Requires [xmake](https://xmake.io) and, on Windows, the Vulkan SDK (for the loader).

```sh
xmake            # configure + build
xmake run lazy100 [cart.lua or cart.lz100]
```

## Status

Under construction — see [docs/DESIGN.md](docs/DESIGN.md) for the architecture and
[docs/TODO.md](docs/TODO.md) for the milestone plan. The runtime kernel (boot, indexed
framebuffer, Lua drawing/font, input, sprites, audio) and the first slice of the in-console
editor suite — command-line shell, `.lz100` cart load/save, and the sprite editor — are in
place; the map, code, and music-tracker editors are next. 中文文档见 [docs/zh_CN/](docs/zh_CN/).

## Credits

- Built-in font: **[Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font)**
  (缝合像素字体, 10px, pan-CJK) by **TakWolf** and contributors, under the
  [SIL OFL 1.1](assets/fonts/fusion-pixel-OFL.txt) — see [assets/fonts/](assets/fonts/).
  Rasterized at runtime with **stb_truetype** so `print()` renders any UTF-8 (Latin + 中日韩).
- Renders through [VRI](https://github.com/zzxzzk115/VRI).

## License

[MIT](./LICENSE) © Lazy_V (project code; bundled assets keep their own licenses — see [Credits](#credits))
