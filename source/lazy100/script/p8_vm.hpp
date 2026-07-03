#pragma once

#include <memory>
#include <string>

namespace lazy100
{
    class Console;

    // The p8-language runtime: a z8lua VM (the PICO-8 Lua fork - fixed-point numbers and the
    // p8 dialect built into the lexer, so `x+=1`, `?"hi"`, `0x3fff.ffff & mask`, and 16.16
    // overflow all work natively). It binds the hardware API (graphics/sprite/map/input/audio/
    // memory) directly in C++ with the 128x128 viewport folded in, and runs a small bios for
    // the pure-Lua table helpers. This replaces the source-transpilation path for p8 carts;
    // native (.lz100) carts keep using LuaRuntime (Lua 5.4 + sol2).
    class P8Vm
    {
    public:
        P8Vm();
        ~P8Vm();

        P8Vm(const P8Vm&)            = delete;
        P8Vm& operator=(const P8Vm&) = delete;

        bool init(Console& console);                // create the VM + bind the API + bios
        bool load_source(const std::string& code);  // fresh VM, run the cart, resolve callbacks

        void call_init();
        void call_update();
        void call_draw(); // draws the cart, then the console-shell chrome frame

        bool has_update() const;
        bool has_draw() const;
        bool wants_60hz() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
