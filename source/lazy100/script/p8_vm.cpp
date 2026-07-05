#include "lazy100/script/p8_vm.hpp"

#include "lazy100/audio/audio.hpp"
#include "lazy100/cart/p8.hpp"
#include "lazy100/cart/p8_font.h"
#include "lazy100/common/log.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/input.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

// z8lua's headers are C++ (lua_Number is the z8::fix32 class), so they are included directly
// rather than wrapped in extern "C". z8prefix.h renames every Lua symbol to z8_* FIRST, so
// this TU links z8lua (not the stock Lua 5.4 that sol2 uses elsewhere) - the two coexist.
#include "z8prefix.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace lazy100
{
    namespace
    {
        // The centered 128x128 viewport within our 320x240 screen.
        constexpr int OX = 96, OY = 56;

        // Single live console (one VM per process; matches the reference emulators' globals).
        Console* g_con = nullptr;

        // Seconds since the cart started; advanced by call_update.
        double g_seconds = 0.0;

        // Persistent pen color (color()): the raw index used by any draw call that omits its
        // color argument. PICO-8 keeps this as draw state - color(c) sets it, and every draw
        // function that takes a color also updates it as a side effect (so `color(11) rectfill(..)`
        // and `line(..,11) rectfill(..)` both fill with 11). Reset to 6 on cart start. Missing this
        // broke 3D carts (e.g. airpico) that `color(face)` then `rectfill()` every model polygon.
        u8 g_pen = 6;

        Console&     con() { return *g_con; }
        Framebuffer& fb() { return g_con->framebuffer(); }

        // Argument helpers over z8lua's fixed-point numbers (lua_Number == fix32; the implicit
        // conversions to int/double are exact 16.16 truncations, matching PICO-8).
        int    ai(lua_State* L, int i) { return static_cast<int>(lua_tonumber(L, i)); }
        int    ai(lua_State* L, int i, int def) { return lua_isnoneornil(L, i) ? def : ai(L, i); }
        double ad(lua_State* L, int i) { return static_cast<double>(lua_tonumber(L, i)); }
        bool   ab(lua_State* L, int i) { return lua_toboolean(L, i) != 0; }
        void   push_i(lua_State* L, int v) { lua_pushnumber(L, z8::fix32(v)); }
        void   push_d(lua_State* L, double v) { lua_pushnumber(L, z8::fix32(v)); }

        // Draw pen: color index remapped by the draw palette (pal). When the color argument is
        // omitted, PICO-8 uses the persistent pen color (g_pen); when it is present, that value
        // also becomes the new pen (drawing-with-a-color is how carts set the pen without color()).
        // The `def` argument is now only the seed for g_pen before any color is chosen.
        u8 pen(lua_State* L, int i, u8 /*def*/)
        {
            if (!lua_isnoneornil(L, i))
                g_pen = static_cast<u8>(ai(L, i) & (kPaletteSize - 1));
            return con().draw_pal()[g_pen & (kPaletteSize - 1)];
        }
        // World coordinate -> screen coordinate (camera already carries the viewport origin).
        int camx(lua_State* L, int i) { return ai(L, i) - con().cam_x(); }
        int camy(lua_State* L, int i) { return ai(L, i) - con().cam_y(); }

        // ---- print cursor + p8 bitmap font ----
        int    g_cur_x = 0, g_cur_y = 0;

        // p8 screen palette (0x5f10-0x5f1f): a display-time LUT, drawn index i -> hardware color
        // idx. It must recolor only the game's 128x128 viewport, never our chrome/shell, so it is
        // NOT written into the global palette (that would repaint the whole 320x240). Instead it
        // updates Console's separate index->index remap, baked into the viewport at present time.
        void screen_pal_refresh(size_t lo, size_t hi)
        {
            if (hi < 0x5f10 || lo > 0x5f1f)
                return;
            auto& ram = con().p8ram();
            if (ram.size() < 0x5f20)
                return;
            auto& sp = con().screen_pal();
            for (u32 i = 0; i < 16; ++i)
            {
                const u8  v   = ram[0x5f10 + i];
                const u32 idx = static_cast<u32>(v & 0x0f) + ((v & 0x80) ? 16u : 0u);
                sp[i]         = static_cast<u8>(idx);
                if (idx != i)
                    con().mark_screen_pal_active();
            }
        }

        // Draw one p8 print string at (sx,sy), honoring \^w/\^t and the \^!addr poke code.
        // Returns the ending x (screen space).
        int p8_print(const char* text, int sx, int sy, u8 c, bool force_builtin = false)
        {
            int          px = sx, py = sy, x0 = sx;
            bool         wide = false, tall = false;
            const std::string s = text ? text : "";
            const size_t n = s.size();
            for (size_t i = 0; i < n;)
            {
                const unsigned char ch  = static_cast<unsigned char>(s[i]);
                const bool          esc = (ch == '\\' && i + 1 < n && s[i + 1] == '^');
                if (esc || ch == 6)
                {
                    size_t k  = i + (esc ? 2 : 1);
                    bool   on = true;
                    if (k < n && s[k] == '-')
                    {
                        on = false;
                        ++k;
                    }
                    if (k < n && s[k] == '!' && k + 4 < n)
                    {
                        size_t addr  = 0;
                        bool   okhex = true;
                        for (size_t h = k + 1; h < k + 5; ++h)
                        {
                            const char hc = s[h];
                            int        hv = -1;
                            if (hc >= '0' && hc <= '9')
                                hv = hc - '0';
                            else if (hc >= 'a' && hc <= 'f')
                                hv = hc - 'a' + 10;
                            else if (hc >= 'A' && hc <= 'F')
                                hv = hc - 'A' + 10;
                            else
                                okhex = false;
                            addr = addr * 16 + static_cast<size_t>(hv < 0 ? 0 : hv);
                        }
                        if (okhex)
                        {
                            auto&  ram = con().p8ram();
                            size_t w   = addr;
                            for (size_t b2 = k + 5; b2 < n && w < ram.size(); ++b2, ++w)
                                ram[w] = static_cast<u8>(s[b2]);
                            screen_pal_refresh(addr, w);
                            break;
                        }
                    }
                    if (k < n)
                    {
                        if (s[k] == 'w')
                            wide = on;
                        else if (s[k] == 't')
                            tall = on;
                    }
                    i = k + 1;
                    continue;
                }
                if (ch == '\n')
                {
                    px = x0;
                    py += 6 * (tall ? 2 : 1);
                    ++i;
                    continue;
                }
                if (ch < 0x10)
                {
                    ++i;
                    continue;
                }
                // Custom font (poke 0x5f58 bit 7): glyph bitmaps live at 0x5600, 8 bytes per
                // character (row r = byte, bit b = column b), fixed 8px cell. Falls back to the
                // built-in font when disabled. Used by carts like marble_merger for their UI text.
                const auto&          ram = con().p8ram();
                const bool           custom = !force_builtin && ram.size() > 0x5f58 &&
                                    (ram[0x5f58] & 0x80) && ram.size() >= 0x5600 + (ch + 1) * 8;
                unsigned char        crows[8];
                const unsigned char* rows;
                if (custom)
                {
                    for (int r = 0; r < 8; ++r)
                        crows[r] = ram[0x5600 + ch * 8 + r];
                    rows = crows;
                }
                else
                    rows = kP8Font[ch];
                const int gw = wide ? 2 : 1, gh = tall ? 2 : 1;
                for (int r = 0; r < 8; ++r)
                    for (int b = 0; b < 8; ++b)
                        if (rows[r] & (1 << b))
                            for (int yy = 0; yy < gh; ++yy)
                                for (int xx = 0; xx < gw; ++xx)
                                    fb().pset(px + b * gw + xx, py + r * gh + yy, c);
                px += (custom ? 8 : (ch < 0x80 ? 4 : 8)) * gw;
                ++i;
            }
            return px;
        }

        // ============================ graphics ============================
        int l_cls(lua_State* L)
        {
            fb().rectfill(OX, OY, OX + 127, OY + 127, static_cast<u8>(ai(L, 1, 0) & 15));
            g_cur_x = OX;
            g_cur_y = OY;
            return 0;
        }
        int l_pset(lua_State* L)
        {
            fb().fpset(camx(L, 1), camy(L, 2), pen(L, 3, 6));
            return 0;
        }
        int l_pget(lua_State* L)
        {
            push_i(L, fb().pget(camx(L, 1), camy(L, 2)));
            return 1;
        }
        int l_rectfill(lua_State* L)
        {
            fb().frectfill(camx(L, 1), camy(L, 2), camx(L, 3), camy(L, 4), pen(L, 5, 6));
            return 0;
        }
        int l_rect(lua_State* L)
        {
            draw::rect(fb(), camx(L, 1), camy(L, 2), camx(L, 3), camy(L, 4), pen(L, 5, 6));
            return 0;
        }
        int l_line(lua_State* L)
        {
            draw::line(fb(), camx(L, 1), camy(L, 2), camx(L, 3), camy(L, 4), pen(L, 5, 6));
            return 0;
        }
        int l_circ(lua_State* L)
        {
            draw::circ(fb(), camx(L, 1), camy(L, 2), ai(L, 3, 4), pen(L, 4, 6));
            return 0;
        }
        int l_circfill(lua_State* L)
        {
            draw::circfill(fb(), camx(L, 1), camy(L, 2), ai(L, 3, 4), pen(L, 4, 6));
            return 0;
        }
        int l_oval(lua_State* L)
        {
            draw::oval(fb(), camx(L, 1), camy(L, 2), camx(L, 3), camy(L, 4), pen(L, 5, 6));
            return 0;
        }
        int l_ovalfill(lua_State* L)
        {
            draw::ovalfill(fb(), camx(L, 1), camy(L, 2), camx(L, 3), camy(L, 4), pen(L, 5, 6));
            return 0;
        }
        int l_print(lua_State* L)
        {
            // print(text) / print(text, color) / print(text, x, y[, color])
            size_t      len = 0;
            const char* t   = luaL_tolstring(L, 1, &len); // any value -> string (top of stack)
            std::string s(t, len);
            lua_pop(L, 1);

            int x, y;
            u8  c;
            const int top = lua_gettop(L);
            if (top >= 3 && !lua_isnil(L, 3)) // explicit x, y
            {
                x = camx(L, 2);
                y = camy(L, 3);
                c = pen(L, 4, 6);
                g_cur_x = OX;
                g_cur_y = y + 6;
            }
            else
            {
                if (top >= 2 && !lua_isnil(L, 2)) // print(text, color)
                    c = pen(L, 2, 6);
                else
                    c = con().draw_pal()[6];
                x = g_cur_x;
                y = g_cur_y;
                g_cur_y += 6;
                if (g_cur_y > OY + 122)
                    g_cur_y = OY + 122;
            }
            const int endx = p8_print(s.c_str(), x, y, c);
            push_i(L, endx);
            return 1;
        }
        int l_cursor(lua_State* L)
        {
            g_cur_x = camx(L, 1) + con().cam_x(); // cursor is in draw space; store screen coords
            g_cur_y = camy(L, 2) + con().cam_y();
            g_cur_x = OX + ai(L, 1, 0);
            g_cur_y = OY + ai(L, 2, 0);
            return 0;
        }
        int l_camera(lua_State* L)
        {
            con().set_camera(ai(L, 1, 0) - OX, ai(L, 2, 0) - OY);
            return 0;
        }
        int l_clip(lua_State* L)
        {
            if (lua_gettop(L) < 4)
            {
                fb().clip(OX, OY, 128, 128);
                return 0;
            }
            int x = ai(L, 1), y = ai(L, 2), w = ai(L, 3), h = ai(L, 4);
            x = std::clamp(x, 0, 128);
            y = std::clamp(y, 0, 128);
            fb().clip(OX + x, OY + y, w, h);
            return 0;
        }
        int l_fillp(lua_State* L)
        {
            const double v      = ad(L, 1);
            const double fl     = std::floor(v);
            const bool   transp = (v - fl) >= 0.5;
            fb().fillp_set(static_cast<u16>(static_cast<long long>(fl) & 0xffff), transp, true, 0);
            return 0;
        }
        // color([c]): set the persistent pen color, return the previous one. color() with no
        // argument resets to 6 (PICO-8 default). Draw calls that omit their color use g_pen.
        int l_color(lua_State* L)
        {
            const u8 prev = g_pen;
            g_pen         = lua_isnoneornil(L, 1) ? 6 : static_cast<u8>(ai(L, 1) & (kPaletteSize - 1));
            push_i(L, prev);
            return 1;
        }

        int l_pal(lua_State* L)
        {
            Palette& pal = con().palette();
            if (lua_gettop(L) == 0 || lua_isnil(L, 1))
            {
                con().reset_draw_pal();
                pal.reset();
                fb().fillp_secondary_reset();
                return 0;
            }
            if (lua_istable(L, 1)) // pal({[c]=v,...}, mode)
            {
                const int mode = ai(L, 2, 0);
                lua_pushnil(L);
                while (lua_next(L, 1) != 0)
                {
                    const int a = ai(L, -2) & (kPaletteSize - 1);
                    int       b = ai(L, -1);
                    if (mode == 1)
                        pal.set(a, pal.default_at(static_cast<u32>(b & (kPaletteSize - 1))));
                    else if (mode == 2)
                        fb().fillp_secondary(a, static_cast<u8>(b & 0xff));
                    else
                        con().draw_pal()[a] = static_cast<u8>(b & (kPaletteSize - 1));
                    lua_pop(L, 1);
                }
                return 0;
            }
            const int a    = ai(L, 1) & (kPaletteSize - 1);
            int       b    = ai(L, 2);
            const int mode = ai(L, 3, 0);
            if (mode == 1)
            {
                if (b >= 128)
                    b = (b % 16) + 16;
                pal.set(a, pal.default_at(static_cast<u32>(b & (kPaletteSize - 1))));
            }
            else if (mode == 2)
                fb().fillp_secondary(a, static_cast<u8>(b & 0xff));
            else
            {
                if (b >= 128)
                    b = (b % 16) + 16;
                con().draw_pal()[a] = static_cast<u8>(b & (kPaletteSize - 1));
            }
            return 0;
        }
        int l_palt(lua_State* L)
        {
            if (lua_gettop(L) == 0 || lua_isnil(L, 1))
            {
                con().reset_transparent();
                return 0;
            }
            con().transparent()[ai(L, 1) & (kPaletteSize - 1)] = ab(L, 2);
            return 0;
        }

        // ============================ sprites ============================
        int l_spr(lua_State* L)
        {
            const int n = ai(L, 1);
            const int x = camx(L, 2), y = camy(L, 3);
            const int w = ai(L, 4, 1), h = ai(L, 5, 1);
            const bool fx = ab(L, 6), fy = ab(L, 7);
            con().sheet().sspr(fb(), (n % 16) * 8, (n / 16) * 8, w * 8, h * 8, x, y, w * 8, h * 8, fx, fy,
                               con().draw_pal(), con().transparent());
            return 0;
        }
        int l_sspr(lua_State* L)
        {
            const int sx = ai(L, 1), sy = ai(L, 2), sw = ai(L, 3), sh = ai(L, 4);
            const int dx = camx(L, 5), dy = camy(L, 6);
            const int dw = ai(L, 7, sw), dh = ai(L, 8, sh);
            const bool fx = ab(L, 9), fy = ab(L, 10);
            con().sheet().sspr(fb(), sx, sy, sw, sh, dx, dy, dw, dh, fx, fy, con().draw_pal(),
                               con().transparent());
            return 0;
        }
        int l_sget(lua_State* L)
        {
            push_i(L, con().sheet().get(ai(L, 1), ai(L, 2)));
            return 1;
        }
        int l_sset(lua_State* L)
        {
            con().sheet().set(ai(L, 1), ai(L, 2), static_cast<u8>(ai(L, 3, 0) & 15));
            return 0;
        }
        int l_fget(lua_State* L)
        {
            const int n = ai(L, 1);
            if (lua_isnoneornil(L, 2))
            {
                push_i(L, con().sheet().flags(n));
                return 1;
            }
            lua_pushboolean(L, con().sheet().flag_bit(n, ai(L, 2)) ? 1 : 0);
            return 1;
        }
        int l_fset(lua_State* L)
        {
            const int n = ai(L, 1);
            if (lua_isnoneornil(L, 3))
                con().sheet().set_flags(n, static_cast<u8>(ai(L, 2) & 0xff));
            else
                con().sheet().set_flag_bit(n, ai(L, 2), ab(L, 3));
            return 0;
        }

        // ============================ map ============================
        int l_mget(lua_State* L)
        {
            push_i(L, con().map().get(ai(L, 1), ai(L, 2)));
            return 1;
        }
        int l_mset(lua_State* L)
        {
            con().map().set(ai(L, 1), ai(L, 2), static_cast<u8>(ai(L, 3, 0) & 0xff));
            return 0;
        }
        int l_map(lua_State* L)
        {
            const int cx = ai(L, 1, 0), cy = ai(L, 2, 0);
            const int sx = ai(L, 3, 0), sy = ai(L, 4, 0);
            const int cw = ai(L, 5, 128), ch = ai(L, 6, 64);
            const int layers = ai(L, 7, 0);
            SpriteSheet& sh = con().sheet();
            for (int j = 0; j < ch; ++j)
                for (int i = 0; i < cw; ++i)
                {
                    const int n = con().map().get(cx + i, cy + j);
                    if (n == 0 || n == 255)
                        continue;
                    if (layers != 0 && (sh.flags(n) & layers) != layers)
                        continue;
                    const int dx = sx + i * 8 - con().cam_x();
                    const int dy = sy + j * 8 - con().cam_y();
                    sh.sspr(fb(), (n % 16) * 8, (n / 16) * 8, 8, 8, dx, dy, 8, 8, false, false,
                            con().draw_pal(), con().transparent());
                }
            return 0;
        }
        int l_tline(lua_State* L)
        {
            const int x0 = camx(L, 1), y0 = camy(L, 2), x1 = camx(L, 3), y1 = camy(L, 4);
            double mx = ad(L, 5), my = ad(L, 6);
            const double mdx = lua_isnoneornil(L, 7) ? 0.125 : ad(L, 7);
            const double mdy = lua_isnoneornil(L, 8) ? 0.0 : ad(L, 8);
            const int    steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
            const double ddx = steps ? double(x1 - x0) / steps : 0;
            const double ddy = steps ? double(y1 - y0) / steps : 0;
            double x = x0, y = y0;

            // tline texture-window registers 0x5f38-0x5f3b: wrap the sampled MAP tile into a
            // (w x h) region at offset (ox,oy). w/h are the wrap period (a power of 2); a period
            // of 0 means no wrap (raw coordinate). Without this, coords that run off the map (e.g.
            // airpico's distant terrain, where my goes negative) sample the "empty" tile and get
            // skipped, leaving coarse holes instead of the tiled ground texture.
            const auto& ram    = con().p8ram();
            const bool  hasreg = ram.size() >= 0x5f3c;
            const int   maskx  = hasreg && ram[0x5f38] ? ram[0x5f38] - 1 : 0;
            const int   masky  = hasreg && ram[0x5f39] ? ram[0x5f39] - 1 : 0;
            const int   offx   = hasreg ? ram[0x5f3a] : 0;
            const int   offy   = hasreg ? ram[0x5f3b] : 0;

            for (int s = 0; s <= steps; ++s)
            {
                int tx = static_cast<int>(std::floor(mx));
                int ty = static_cast<int>(std::floor(my));
                if (maskx)
                    tx = (tx & maskx) + offx;
                if (masky)
                    ty = (ty & masky) + offy;
                const int t = con().map().get(tx, ty);
                if (t != 0 && t != 255)
                {
                    const int px = (t % 16) * 8 + (static_cast<int>(std::floor(mx * 8)) & 7);
                    const int py = (t / 16) * 8 + (static_cast<int>(std::floor(my * 8)) & 7);
                    const u8  c  = con().sheet().get(px, py);
                    if (c != 0)
                        fb().pset(static_cast<int>(x), static_cast<int>(y), con().draw_pal()[c]);
                }
                x += ddx;
                y += ddy;
                mx += mdx;
                my += mdy;
            }
            return 0;
        }

        // ============================ input ============================
        int l_btn(lua_State* L)
        {
            Input& in = con().input();
            if (lua_isnoneornil(L, 1))
            {
                push_i(L, static_cast<int>(in.held_mask(ai(L, 2, 0))));
                return 1;
            }
            lua_pushboolean(L, in.held(ai(L, 1), ai(L, 2, 0)) ? 1 : 0);
            return 1;
        }
        int l_btnp(lua_State* L)
        {
            Input& in = con().input();
            if (lua_isnoneornil(L, 1))
            {
                push_i(L, static_cast<int>(in.pressed_mask(ai(L, 2, 0))));
                return 1;
            }
            lua_pushboolean(L, in.pressed(ai(L, 1), ai(L, 2, 0)) ? 1 : 0);
            return 1;
        }

        // ============================ memory ============================
        // A p8 address is a 16-bit value. Hex literals like 0x8000 are SIGNED 16.16 fixed
        // point, so `0x8000` reads as -32768; PICO-8 (and z8lua's native @/%/$) mask to
        // 0..0xffff. Our peek/poke bindings must do the same or hi-mem addresses become huge
        // size_t values that fail the bounds check (silently dropping every write >= 0x8000).
        size_t p8addr(lua_State* L, int i) { return static_cast<size_t>(ai(L, i) & 0xffff); }

        u8   peek1(size_t a)
        {
            const auto& ram = con().p8ram();
            return a < ram.size() ? ram[a] : 0;
        }
        void poke1(size_t a, u8 v)
        {
            auto& ram = con().p8ram();
            if (a < ram.size())
                ram[a] = v;
        }
        int l_peek(lua_State* L)
        {
            const size_t a = p8addr(L, 1);
            const int    n = ai(L, 2, 1);
            for (int k = 0; k < std::max(1, n); ++k)
                push_i(L, peek1(a + k));
            return std::max(1, n);
        }
        int l_poke(lua_State* L)
        {
            const size_t a   = p8addr(L, 1);
            const int    top = lua_gettop(L);
            for (int k = 2; k <= std::max(2, top); ++k)
                poke1(a + (k - 2), static_cast<u8>(ai(L, k, 0) & 0xff));
            screen_pal_refresh(a, a + std::max(1, top - 1));
            return 0;
        }
        int l_peek2(lua_State* L)
        {
            const size_t a = p8addr(L, 1);
            push_i(L, static_cast<int16_t>(peek1(a) | (peek1(a + 1) << 8)));
            return 1;
        }
        int l_poke2(lua_State* L)
        {
            const size_t a = p8addr(L, 1);
            const int    v = ai(L, 2);
            poke1(a, static_cast<u8>(v & 0xff));
            poke1(a + 1, static_cast<u8>((v >> 8) & 0xff));
            screen_pal_refresh(a, a + 1);
            return 0;
        }
        int l_peek4(lua_State* L)
        {
            const size_t a = p8addr(L, 1);
            const int32_t bits = static_cast<int32_t>(peek1(a) | (peek1(a + 1) << 8) |
                                                      (peek1(a + 2) << 16) | (peek1(a + 3) << 24));
            lua_pushnumber(L, z8::fix32::frombits(bits));
            return 1;
        }
        int l_poke4(lua_State* L)
        {
            const size_t  a    = p8addr(L, 1);
            const int32_t bits = lua_tonumber(L, 2).bits();
            poke1(a, static_cast<u8>(bits & 0xff));
            poke1(a + 1, static_cast<u8>((bits >> 8) & 0xff));
            poke1(a + 2, static_cast<u8>((bits >> 16) & 0xff));
            poke1(a + 3, static_cast<u8>((bits >> 24) & 0xff));
            return 0;
        }
        int l_memcpy(lua_State* L)
        {
            auto&        ram = con().p8ram();
            const size_t d = p8addr(L, 1), s = p8addr(L, 2);
            size_t       n = static_cast<size_t>(std::max(0, ai(L, 3)));
            if (d >= ram.size() || s >= ram.size())
                return 0;
            n = std::min({n, ram.size() - d, ram.size() - s});
            std::memmove(ram.data() + d, ram.data() + s, n);
            screen_pal_refresh(d, d + n);
            return 0;
        }
        int l_memset(lua_State* L)
        {
            auto&        ram = con().p8ram();
            const size_t d = p8addr(L, 1);
            if (d >= ram.size())
                return 0;
            const size_t n = std::min(static_cast<size_t>(std::max(0, ai(L, 3))), ram.size() - d);
            std::memset(ram.data() + d, ai(L, 2) & 0xff, n);
            screen_pal_refresh(d, d + n);
            return 0;
        }

        // ============================ audio ============================
        SoundBank decode_ram_bank()
        {
            SoundBank b;
            if (con().p8_mode() && con().p8ram().size() >= 0x4300)
                p8::decode_audio_ram(con().p8ram().data() + 0x3100, b);
            else
                b = con().sounds();
            return b;
        }
        int l_sfx(lua_State* L)
        {
            const int i  = ai(L, 1);
            const int ch = ai(L, 2, -1);
            if (i == -1)
            {
                con().audio().stop_sfx(ch);
                return 0;
            }
            if (i == -2)
            {
                con().audio().release_sfx_loop(ch);
                return 0;
            }
            if (i < 0 || i >= SoundBank::kSfxCount)
                return 0;
            SoundBank  bank = decode_ram_bank();
            SfxPattern pat  = bank.sfx[i];
            const int  off = std::clamp(ai(L, 3, 0), 0, SfxPattern::kSteps - 1);
            const int  len = std::clamp(ai(L, 4, SfxPattern::kSteps), 1, SfxPattern::kSteps - off);
            if (off > 0 || len < SfxPattern::kSteps)
            {
                SfxPattern sl;
                sl.speed = pat.speed;
                for (int s = 0; s < len; ++s)
                    sl.notes[s] = pat.notes[off + s];
                if (pat.loops())
                {
                    sl.loop_start = static_cast<u8>(std::max(0, pat.loop_start - off));
                    sl.loop_end   = static_cast<u8>(std::clamp(pat.loop_end - off, 0, len));
                }
                else if (len < SfxPattern::kSteps)
                    sl.loop_start = static_cast<u8>(len);
                pat = sl;
            }
            con().audio().play_sfx(pat, ch);
            return 0;
        }
        int l_music(lua_State* L)
        {
            const int i = ai(L, 1, 0);
            if (i < 0)
                con().audio().stop_music();
            else if (i < SoundBank::kMusicCount)
                con().audio().play_music(i, decode_ram_bank());
            return 0;
        }

        // ============================ misc ============================
        int l_time(lua_State* L)
        {
            push_d(L, g_seconds);
            return 1;
        }
        int l_stat(lua_State* L)
        {
            const int n = ai(L, 1, 0);
            // Minimal: 16-19 = current music/sfx channels; 24 = current music pattern; else 0.
            if (n == 24)
                push_i(L, con().audio().music_pattern());
            else
                push_i(L, 0);
            return 1;
        }
        // PRNG: pico8 rnd([x]) -> [0,x) (default 1), or a random element of a table. srand(s).
        u32 g_rng = 0x1234567u;
        int l_rnd(lua_State* L)
        {
            g_rng ^= g_rng << 13;
            g_rng ^= g_rng >> 17;
            g_rng ^= g_rng << 5;
            const double r = static_cast<double>(g_rng) / 4294967296.0; // [0,1)
            if (lua_istable(L, 1))
            {
                const int n = static_cast<int>(lua_rawlen(L, 1));
                if (n <= 0)
                {
                    lua_pushnil(L);
                    return 1;
                }
                lua_rawgeti(L, 1, static_cast<int>(r * n) + 1);
                return 1;
            }
            const double x = lua_isnoneornil(L, 1) ? 1.0 : ad(L, 1);
            push_d(L, r * x);
            return 1;
        }
        int l_srand(lua_State* L)
        {
            const u32 s = static_cast<u32>(static_cast<int>(lua_tonumber(L, 1).bits()));
            g_rng       = s ? s : 0x1234567u;
            return 0;
        }
        int l_printh(lua_State*) { return 0; }
        int l_menuitem(lua_State*) { return 0; }
        int l_extcmd(lua_State*) { return 0; }
        int l_flip(lua_State*) { return 0; }
        int l_reload(lua_State*) { return 0; }
        int l_cstore(lua_State*) { return 0; }
        int l_load(lua_State* L)
        {
            if (lua_isstring(L, 1))
                con().request_cart_load(lua_tostring(L, 1));
            return 0;
        }
        int l_cartdata(lua_State* L)
        {
            if (lua_isstring(L, 1))
                con().cartdata_open(lua_tostring(L, 1));
            return 0;
        }
        int l_dget(lua_State* L)
        {
            push_d(L, con().cartdata_get(ai(L, 1)));
            return 1;
        }
        int l_dset(lua_State* L)
        {
            con().cartdata_set(ai(L, 1), ad(L, 2));
            return 0;
        }
    } // namespace

    struct P8Vm::Impl
    {
        lua_State* L        = nullptr;
        bool       has_init = false, has_update = false, has_update60 = false, has_draw = false;

        ~Impl()
        {
            if (L)
                lua_close(L);
        }

        void bind()
        {
            const luaL_Reg fns[] = {
                {"cls", l_cls},         {"pset", l_pset},         {"pget", l_pget},
                {"rectfill", l_rectfill}, {"rect", l_rect},       {"line", l_line},
                {"circ", l_circ},       {"circfill", l_circfill}, {"oval", l_oval},
                {"ovalfill", l_ovalfill}, {"print", l_print},     {"cursor", l_cursor},
                {"camera", l_camera},   {"clip", l_clip},         {"fillp", l_fillp},
                {"color", l_color},     {"pal", l_pal},           {"palt", l_palt},
                {"spr", l_spr},         {"sspr", l_sspr},         {"sget", l_sget},
                {"sset", l_sset},       {"fget", l_fget},         {"fset", l_fset},
                {"mget", l_mget},       {"mset", l_mset},         {"map", l_map},
                {"mapdraw", l_map},     {"tline", l_tline},       {"btn", l_btn},
                {"btnp", l_btnp},       {"peek", l_peek},         {"poke", l_poke},
                {"peek2", l_peek2},     {"poke2", l_poke2},       {"peek4", l_peek4},
                {"poke4", l_poke4},     {"memcpy", l_memcpy},     {"memset", l_memset},
                {"sfx", l_sfx},         {"music", l_music},       {"time", l_time},
                {"t", l_time},          {"stat", l_stat},         {"rnd", l_rnd},
                {"srand", l_srand},     {"printh", l_printh},
                {"menuitem", l_menuitem}, {"extcmd", l_extcmd},   {"flip", l_flip},
                {"reload", l_reload},   {"cstore", l_cstore},     {"load", l_load},
                {"cartdata", l_cartdata}, {"dget", l_dget},       {"dset", l_dset},
                {nullptr, nullptr}};
            for (const luaL_Reg* r = fns; r->name; ++r)
            {
                lua_pushcfunction(L, r->func);
                lua_setglobal(L, r->name);
            }
        }
    };

    // Pure-Lua helpers not built into z8lua's libs (table ops, coroutine aliases). Runs at init.
    static const char* kBios = R"lua(
add = function(t, v, i)
  if t == nil then return end
  if i == nil then t[#t + 1] = v else table.insert(t, i, v) end
  return v
end
del = function(t, v)
  if t == nil then return end
  for i = 1, #t do if t[i] == v then table.remove(t, i) return v end end
end
deli = function(t, i)
  if t == nil then return end
  if i == nil then return table.remove(t) end
  return table.remove(t, i)
end
count = function(t, v)
  if t == nil then return 0 end
  if v == nil then return #t end
  local n = 0 for i = 1, #t do if t[i] == v then n = n + 1 end end return n
end
all = function(t)
  if t == nil or #t == 0 then return function() end end
  local i, prev = 1, nil
  return function()
    if t[i] == prev then i = i + 1 end
    while i <= #t and t[i] == nil do i = i + 1 end
    prev = t[i]
    return prev
  end
end
foreach = function(t, f) for v in all(t) do f(v) end end
local _pairs = pairs
pairs = function(t) if t == nil then return function() end end return _pairs(t) end
cocreate = coroutine.create
coresume = coroutine.resume
costatus = coroutine.status
yield = coroutine.yield
sub = string.sub
unpack = unpack or table.unpack
pack = table.pack
-- string indexing (s[i] -> s:sub(i,i)), as PICO-8 provides
getmetatable('').__index = function(s, i)
  if type(i) == "number" then return string.sub(s, i, i) else return string[i] end
end
)lua";

    P8Vm::P8Vm()  = default;
    P8Vm::~P8Vm() = default;

    bool P8Vm::init(Console& console)
    {
        g_con = &console;
        p_    = std::make_unique<Impl>();
        return true;
    }

    bool P8Vm::load_source(const std::string& code)
    {
        Impl& im = *p_;
        if (im.L)
            lua_close(im.L);
        im.L = luaL_newstate();
        luaL_openlibs(im.L);
        im.bind();
        g_seconds = 0.0;
        g_pen     = 6;
        // Reset the screen palette to identity BEFORE the cart's top-level code runs below - some
        // carts (e.g. marble_merger) set a screen palette at load time via a `?"\^!5f10..."` poke.
        // Resetting after load_source (as start_cart used to) would wipe that legitimate setup.
        con().reset_screen_pal();

        // Point z8lua's native @/%/$ peek operators at our 64KB p8 RAM. z8lua compiles these
        // operators to luaV_peek(G(L)->pico8memory, ...); if left unset that pointer is garbage
        // and its guard (`ram && addr<0x10000`) still dereferences it -> crash. The buffer is
        // stable for this cart's life (a load() swap recreates the whole VM).
        auto& ram = con().p8ram();
        if (ram.size() < 0x10000)
            ram.resize(0x10000, 0);
        lua_setpico8memory(im.L, ram.data());

        // Reset the custom-font hardware registers (0x5f58-0x5f5d) to off BEFORE the cart runs.
        // A multi-cart load() reuses the RAM buffer (resize won't re-zero it) and hi-mem is kept
        // across the swap, so these can hold stale values from the loader cart - airpico leaves
        // 0x5f58=0xab (bit 7), which would make p8_print read its model-data region at 0x5600 as
        // font glyphs and scramble all text. PICO-8 starts a cart with the custom font disabled.
        for (size_t a = 0x5f58; a <= 0x5f5d && a < ram.size(); ++a)
            ram[a] = 0;

        if (luaL_dostring(im.L, kBios) != LUA_OK)
        {
            LZ_ERROR("p8 bios: %s", lua_tostring(im.L, -1));
            return false;
        }
        // Register the globals table as the cart sandbox: z8lua's VM falls back to it for
        // global reads when a cart shadows _ENV (the `_ENV = obj` idiom - draw a table's
        // fields as if they were locals). Without this, circfill/spr/... would be nil there.
        lua_pushglobaltable(im.L);
        lua_setfield(im.L, LUA_REGISTRYINDEX, "__PICO8_SANDBOX");
        // '#include' is a p8-tool preprocessor directive with no runtime meaning here; comment
        // out any such line so z8lua's parser doesn't choke on the '#'.
        std::string src = code;
        for (size_t i = 0; i < src.size();)
        {
            const size_t nl = src.find('\n', i);
            const size_t ws = src.find_first_not_of(" \t", i);
            if (ws != std::string::npos && ws < (nl == std::string::npos ? src.size() : nl) &&
                src.compare(ws, 8, "#include") == 0)
            {
                src[i] = '-';
                if (i + 1 < src.size())
                    src[i + 1] = '-';
            }
            if (nl == std::string::npos)
                break;
            i = nl + 1;
        }
        // Button glyphs (⬅️➡️⬆️⬇️🅾️❎ = bytes 0x8b/91/94/83/8e/97) are number constants 0-5 in
        // the p8 dialect, but z8lua's lexer treats high bytes as identifier chars, so `btn(❎)`
        // would read as `btn(nil)`. Map them to their digit outside strings/comments. (Other
        // high glyphs are left as identifier chars - undefined ones read nil, which is fine.)
        {
            bool inStr = false, inComment = false, inBlock = false;
            char quote = 0;
            for (size_t i = 0; i < src.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(src[i]);
                if (inComment)
                {
                    if (c == '\n')
                        inComment = false;
                    continue;
                }
                if (inBlock)
                {
                    if (c == ']' && i + 1 < src.size() && src[i + 1] == ']')
                    {
                        inBlock = false;
                        ++i;
                    }
                    continue;
                }
                if (inStr)
                {
                    if (c == '\\' && i + 1 < src.size())
                        ++i;
                    else if (c == quote)
                        inStr = false;
                    continue;
                }
                if (c == '"' || c == '\'')
                {
                    inStr = true;
                    quote = static_cast<char>(c);
                    continue;
                }
                if (c == '-' && i + 1 < src.size() && src[i + 1] == '-')
                {
                    inComment = true;
                    ++i;
                    continue;
                }
                if (c == '[' && i + 1 < src.size() && src[i + 1] == '[')
                {
                    inBlock = true;
                    ++i;
                    continue;
                }
                int btn = -1;
                switch (c)
                {
                    case 0x8B: btn = 0; break;
                    case 0x91: btn = 1; break;
                    case 0x94: btn = 2; break;
                    case 0x83: btn = 3; break;
                    case 0x8E: btn = 4; break;
                    case 0x97: btn = 5; break;
                    default: break;
                }
                if (btn >= 0)
                    src[i] = static_cast<char>('0' + btn);
            }
        }
        if (const char* dump = std::getenv("LZ100_P8_RAW"))
            std::ofstream(dump, std::ios::binary) << src;
        if (luaL_dostring(im.L, src.c_str()) != LUA_OK)
        {
            const char* msg = lua_tostring(im.L, -1);
            LZ_ERROR("cart error: %s", msg ? msg : "unknown");
            con().set_last_error(msg ? msg : "unknown p8 error"); // shown by the code editor
            return false;
        }
        auto has = [&](const char* n)
        {
            lua_getglobal(im.L, n);
            const bool ok = lua_isfunction(im.L, -1);
            lua_pop(im.L, 1);
            return ok;
        };
        im.has_init     = has("_init");
        im.has_update   = has("_update");
        im.has_update60 = has("_update60");
        im.has_draw     = has("_draw");
        return true;
    }

    namespace
    {
        void call_cb(lua_State* L, const char* name)
        {
            lua_getglobal(L, name);
            if (!lua_isfunction(L, -1))
            {
                lua_pop(L, 1);
                return;
            }
            if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            {
                const char* msg = lua_tostring(L, -1);
                LZ_ERROR("%s() error: %s", name, msg ? msg : "unknown");
                con().set_last_error(msg ? msg : "unknown p8 error"); // code editor error bar
                lua_pop(L, 1);
            }
        }

        // Console-shell chrome, drawn in screen space each frame around the viewport.
        void draw_chrome()
        {
            Framebuffer& f = fb();
            const u32    save = f.fillp_save();
            f.fillp_restore(0);
            const int prevcx = con().cam_x(), prevcy = con().cam_y();
            con().set_camera(0, 0);
            f.clip_reset();
            f.rectfill(0, 0, 319, OY - 3, 1);
            f.rectfill(0, OY + 130, 319, 239, 1);
            f.rectfill(0, OY - 2, OX - 4, OY + 129, 1);
            f.rectfill(OX + 131, OY - 2, 319, OY + 129, 1);
            draw::rect(f, OX - 3, OY - 3, OX + 130, OY + 130, 13);
            draw::rect(f, OX - 2, OY - 2, OX + 129, OY + 129, 5);
            draw::rect(f, OX - 1, OY - 1, OX + 128, OY + 128, 0);
            // Bottom label row: sit it a few px below the frame (OY+130) so it doesn't hug the
            // viewport - mirrors the breathing room the "LAZY-100" title gets above the frame.
            const int by = OY + 141, bx = OX;
            f.rectfill(bx, by, bx + 5, by + 3, 8);
            f.rectfill(bx + 6, by, bx + 11, by + 3, 9);
            f.rectfill(bx + 12, by, bx + 17, by + 3, 10);
            f.rectfill(bx + 18, by, bx + 23, by + 3, 11);
            p8_print("p8 ext cartridge", OX + 30, OY + 138, 6, true); // chrome: always built-in font
            p8_print("LAZY-100", OX + 90, OY - 12, 13, true);
            con().set_camera(prevcx, prevcy);
            f.fillp_restore(save);
        }
    } // namespace

    void P8Vm::call_init()
    {
        // Clean per-cart draw state, center the viewport, wipe once, draw the frame.
        con().set_camera(-OX, -OY);
        fb().clip(OX, OY, 128, 128);
        con().framebuffer().fillp_reset();
        con().reset_draw_pal();
        con().reset_transparent();
        g_cur_x = OX;
        g_cur_y = OY;
        // full-screen wipe (top-level cart code may draw before _init)
        {
            const int pcx = con().cam_x(), pcy = con().cam_y();
            con().set_camera(0, 0);
            fb().clip_reset();
            fb().rectfill(0, 0, 319, 239, 0);
            con().set_camera(pcx, pcy);
            fb().clip(OX, OY, 128, 128);
        }
        call_cb(p_->L, "_init");
    }
    void P8Vm::call_update()
    {
        g_seconds += (p_->has_update60 ? 1.0 / 60.0 : 1.0 / 30.0);
        call_cb(p_->L, p_->has_update60 ? "_update60" : "_update");
    }
    void P8Vm::call_draw()
    {
        call_cb(p_->L, "_draw");
        draw_chrome();
    }

    bool P8Vm::has_update() const { return p_->has_update || p_->has_update60; }
    bool P8Vm::has_draw() const { return p_->has_draw; }
    bool P8Vm::wants_60hz() const { return p_->has_update60; }
} // namespace lazy100
