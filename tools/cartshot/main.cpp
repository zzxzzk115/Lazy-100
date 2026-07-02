// cartshot - render a cart's first frame headlessly and write it as a 320x240 PNG.
// Usage: cartshot <cart.lz100|cart.png|cart.lua> <out.png>
// Generates the preview images the Lazy-100-games catalog requires (exact palette colors,
// no window or GPU needed), so it also suits CI.
#include "lazy100/console/console.hpp"

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: cartshot <cart.lz100|cart.png|cart.lua> <out.png>\n");
        return 2;
    }
    lazy100::Console console;
    if (!console.headless_shot(argv[1], argv[2]))
    {
        std::fprintf(stderr, "cartshot: failed to render %s\n", argv[1]);
        return 1;
    }
    std::printf("cartshot: %s -> %s (320x240)\n", argv[1], argv[2]);
    return 0;
}
