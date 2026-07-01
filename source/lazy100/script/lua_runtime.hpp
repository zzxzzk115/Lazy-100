#pragma once

#include <memory>
#include <string>

namespace lazy100
{
    class Console;

    // Owns the Lua VM (sol::state, hidden behind the pimpl so sol2 stays out of headers),
    // loads a cart, and calls its _init/_update/_draw callbacks with error containment.
    class LuaRuntime
    {
    public:
        LuaRuntime();
        ~LuaRuntime();

        LuaRuntime(const LuaRuntime&)            = delete;
        LuaRuntime& operator=(const LuaRuntime&) = delete;

        bool init(Console& console);            // create the VM + bind the API
        bool load_source(const std::string& code); // fresh VM, run the cart code, resolve callbacks

        void call_init();
        void call_update();
        void call_draw();

        bool has_update() const;
        bool has_draw() const;
        bool wants_60hz() const; // true if the cart defines _update60

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
