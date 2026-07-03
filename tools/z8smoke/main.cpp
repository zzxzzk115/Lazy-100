// z8smoke: sanity-check the vendored z8lua fork - the p8 dialect must parse natively and
// numbers must behave as 16.16 fixed point (overflow wrap, fractional shifts).

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <cstdio>

int main()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    const char* script = R"(
        -- fixed-point: 0x7fff.ffff + 1 ulp wraps negative
        x = 0x7fff.ffff
        x += 0x0.0001
        assert(x < 0, "fixed-point wrap")

        -- fractional shift (16.16 semantics)
        y = 5
        y >>= 1
        assert(y == 2.5, "fractional shift")

        -- dialect: short if, != and bitwise on fractions
        s = ""
        if (x < 0) s = "wrap"
        assert(s != "", "short if / !=")
        m = 0x3fff.ffff & 0x0000.ffff
        assert(m == 0x0000.ffff, "fractional mask")

        -- integer division and xor operators
        assert(7 \ 2 == 3, "backslash division")
        assert((0b1010 ^^ 0b0110) == 0b1100, "xor")
        return 1
    )";

    if (luaL_dostring(L, script) != LUA_OK)
    {
        std::printf("z8smoke FAIL: %s\n", lua_tostring(L, -1));
        return 1;
    }
    std::printf("z8smoke: all dialect + fixed-point checks passed\n");
    lua_close(L);
    return 0;
}
