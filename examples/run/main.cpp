// Lazy-100 host entry point. `xmake run lazy100 [cart.lua]`.
// M0 just boots the console and shows a window; cart loading arrives in M2.
#include "lazy100/console/console.hpp"

int main(int /*argc*/, char** /*argv*/)
{
    lazy100::Console console;
    if (!console.boot())
        return 1;
    console.run();
    console.shutdown();
    return 0;
}
