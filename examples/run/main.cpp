// Lazy-100 host entry point. `xmake run lazy100 [cart.lua]`.
// With a cart path it loads and runs it; with none it shows the test pattern.
#include "lazy100/console/console.hpp"

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>

namespace
{
    lazy100::Console* g_console = nullptr; // set once boot() succeeds; valid for the page's life
}

// Boot a cart straight into Running from JS: the site writes the fetched .lz100.png bytes into
// MEMFS (FS.writeFile) then ccalls this with the path, so a clicked catalog cart plays in place
// with no shell load/run step. Returns 1 on success. The click itself is the user gesture that
// resumes the AudioContext (see the window handlers in audio.cpp).
extern "C" EMSCRIPTEN_KEEPALIVE int lazy100_boot_cart(const char* path)
{
    if (!g_console || !path)
        return 0;
    return g_console->restart_with_cart(path) ? 1 : 0; // replays the boot splash, then runs the cart
}
#endif

int main(int argc, char** argv)
{
    const char* cart = argc > 1 ? argv[1] : nullptr;

    lazy100::Console console;
    if (!console.boot(cart))
        return 1;
#if defined(__EMSCRIPTEN__)
    g_console = &console; // run() never returns on web (simulated infinite loop), so this stays valid
#endif
    console.run();
    console.shutdown();
    return 0;
}
