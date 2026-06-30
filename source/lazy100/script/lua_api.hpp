#pragma once

#include <sol/sol.hpp>

namespace lazy100
{
    class Console;

    // Bind the console's draw/input/math API as Lua globals onto `lua`. The closures capture
    // the Console so cart code calls straight into the framebuffer/palette. This is the only
    // place (with lua_runtime.cpp) that includes sol2.
    void bind_api(sol::state& lua, Console& console);
} // namespace lazy100
