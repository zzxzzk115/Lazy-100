// Lazy-100 host entry point. `xmake run lazy100 [cart.lua]`.
// With a cart path it loads and runs it; with none it shows the test pattern.
#include "lazy100/console/console.hpp"

int main(int argc, char** argv)
{
    const char* cart = argc > 1 ? argv[1] : nullptr;

    lazy100::Console console;
    if (!console.boot(cart))
        return 1;
    console.run();
    console.shutdown();
    return 0;
}
