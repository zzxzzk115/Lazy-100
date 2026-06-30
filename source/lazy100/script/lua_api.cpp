#include "lazy100/script/lua_api.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/input.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>

namespace lazy100
{
    namespace
    {
        std::mt19937& rng()
        {
            static std::mt19937 g {0x1234567u};
            return g;
        }

        double now_seconds()
        {
            static const auto start = std::chrono::steady_clock::now();
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        // Lua numbers are doubles; the console floors coordinates (PICO-8 semantics) so sol2
        // never has to reject a fractional float as an int argument.
        int fi(double v) { return static_cast<int>(std::floor(v)); }
        int fi(sol::optional<double> v, int def) { return v ? fi(*v) : def; }

        // Raw color index (wraps into the palette range); default when omitted.
        u8 col(sol::optional<double> c, u8 def)
        {
            return c ? static_cast<u8>(static_cast<u32>(fi(*c)) & (kPaletteSize - 1)) : def;
        }

        std::string to_text(const sol::object& o)
        {
            if (o.is<std::string>())
                return o.as<std::string>();
            if (o.is<bool>())
                return o.as<bool>() ? "true" : "false";
            if (o.is<double>())
            {
                const double d = o.as<double>();
                char         buf[32];
                if (d == static_cast<double>(static_cast<long long>(d)))
                    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
                else
                    std::snprintf(buf, sizeof(buf), "%g", d);
                return buf;
            }
            return "";
        }
    } // namespace

    void bind_api(sol::state& lua, Console& console)
    {
        Framebuffer& fb    = console.framebuffer();
        Input&       in    = console.input();
        SpriteSheet& sheet = console.sheet();
        Palette&     pal   = console.palette();
        u8*          dpal  = console.draw_pal();      // live draw-palette remap
        bool*        trans = console.transparent();   // live transparency mask

        // Shape pen: resolve a color arg, then apply the draw-palette remap.
        auto pen = [dpal](sol::optional<double> c, u8 def) -> u8 { return dpal[col(c, def)]; };

        // ---- graphics (coords floored; pen remapped by pal()) ----
        lua.set_function("cls", [&fb](sol::optional<double> c) { fb.cls(col(c, 0)); });
        lua.set_function("pset",
                         [&fb, pen](double x, double y, sol::optional<double> c) { fb.pset(fi(x), fi(y), pen(c, 7)); });
        lua.set_function("pget", [&fb](double x, double y) { return static_cast<int>(fb.pget(fi(x), fi(y))); });
        lua.set_function("rectfill",
                         [&fb, pen](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { fb.rectfill(fi(x0), fi(y0), fi(x1), fi(y1), pen(c, 7)); });
        lua.set_function("rect",
                         [&fb, pen](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { draw::rect(fb, fi(x0), fi(y0), fi(x1), fi(y1), pen(c, 7)); });
        lua.set_function("line",
                         [&fb, pen](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { draw::line(fb, fi(x0), fi(y0), fi(x1), fi(y1), pen(c, 7)); });
        lua.set_function("circ",
                         [&fb, pen](double x, double y, double r, sol::optional<double> c)
                         { draw::circ(fb, fi(x), fi(y), fi(r), pen(c, 7)); });
        lua.set_function("circfill",
                         [&fb, pen](double x, double y, double r, sol::optional<double> c)
                         { draw::circfill(fb, fi(x), fi(y), fi(r), pen(c, 7)); });
        lua.set_function("print",
                         [&fb, pen](sol::object text, sol::optional<double> x, sol::optional<double> y,
                                    sol::optional<double> c)
                         { return font::print(fb, to_text(text).c_str(), fi(x, 0), fi(y, 0), pen(c, 7)); });

        // ---- sprites ----
        lua.set_function("spr",
                         [&sheet, &fb, dpal, trans](double n, double x, double y, sol::optional<double> w,
                                                    sol::optional<double> h, sol::optional<bool> fx,
                                                    sol::optional<bool> fy)
                         {
                             sheet.spr(fb, fi(n), fi(x), fi(y), fi(w, 1), fi(h, 1), fx.value_or(false),
                                       fy.value_or(false), dpal, trans);
                         });
        lua.set_function("sspr",
                         [&sheet, &fb, dpal, trans](double sx, double sy, double sw, double sh, double dx, double dy,
                                                    sol::optional<double> dw, sol::optional<double> dh,
                                                    sol::optional<bool> fx, sol::optional<bool> fy)
                         {
                             sheet.sspr(fb, fi(sx), fi(sy), fi(sw), fi(sh), fi(dx), fi(dy), fi(dw, fi(sw)),
                                        fi(dh, fi(sh)), fx.value_or(false), fy.value_or(false), dpal, trans);
                         });
        lua.set_function("sget", [&sheet](double x, double y) { return static_cast<int>(sheet.get(fi(x), fi(y))); });
        lua.set_function("sset",
                         [&sheet](double x, double y, sol::optional<double> c) { sheet.set(fi(x), fi(y), col(c, 0)); });
        lua.set_function("fset",
                         [&sheet](double n, sol::optional<double> a, sol::optional<bool> b)
                         {
                             if (b)
                                 sheet.set_flag_bit(fi(n), fi(a, 0), *b);
                             else
                                 sheet.set_flags(fi(n), static_cast<u8>(fi(a, 0) & 0xff));
                         });
        lua.set_function("fget",
                         [&sheet](sol::this_state ts, double n, sol::optional<double> bit) -> sol::object
                         {
                             sol::state_view L(ts);
                             if (bit)
                                 return sol::make_object(L, sheet.flag_bit(fi(n), fi(*bit)));
                             return sol::make_object(L, static_cast<int>(sheet.flags(fi(n))));
                         });

        // ---- palette: pal() draw/screen remap, palt() transparency ----
        lua.set_function("pal",
                         [dpal, &pal, &console](sol::optional<double> c0, sol::optional<double> c1,
                                                sol::optional<double> p)
                         {
                             if (!c0 || !c1)
                             {
                                 console.reset_draw_pal();
                                 pal.reset(); // screen palette back to default
                                 return;
                             }
                             const int a = fi(*c0) & (kPaletteSize - 1);
                             const int b = fi(*c1) & (kPaletteSize - 1);
                             if (fi(p, 0) == 1)
                                 pal.set(a, pal.default_at(b)); // screen: index a shows b's default color
                             else
                                 dpal[a] = static_cast<u8>(b); // draw: recolor a -> b on blit
                         });
        lua.set_function("palt",
                         [trans, &console](sol::optional<double> c, sol::optional<bool> t)
                         {
                             if (!c)
                             {
                                 console.reset_transparent();
                                 return;
                             }
                             trans[fi(*c) & (kPaletteSize - 1)] = t.value_or(true);
                         });

        // ---- input (btn(i)/btnp(i) -> bool; no index -> bitmask) ----
        lua.set_function("btn",
                         [&in](sol::this_state ts, sol::optional<double> i, sol::optional<double> p) -> sol::object
                         {
                             sol::state_view L(ts);
                             const int       player = fi(p, 0);
                             if (i)
                                 return sol::make_object(L, in.held(fi(*i), player));
                             return sol::make_object(L, static_cast<int>(in.held_mask(player)));
                         });
        lua.set_function("btnp",
                         [&in](sol::this_state ts, sol::optional<double> i, sol::optional<double> p) -> sol::object
                         {
                             sol::state_view L(ts);
                             const int       player = fi(p, 0);
                             if (i)
                                 return sol::make_object(L, in.pressed(fi(*i), player));
                             return sol::make_object(L, static_cast<int>(in.pressed_mask(player)));
                         });

        // ---- math helpers carts expect ----
        // flr/ceil return integers so concatenations read "14", not "14.0".
        lua.set_function("flr", [](double x) { return static_cast<long long>(std::floor(x)); });
        lua.set_function("ceil", [](double x) { return static_cast<long long>(std::ceil(x)); });
        lua.set_function("abs", [](double x) { return std::fabs(x); });
        lua.set_function("min", [](double a, double b) { return std::min(a, b); });
        lua.set_function("max", [](double a, double b) { return std::max(a, b); });
        lua.set_function("mid",
                         [](double a, double b, double c)
                         { return std::max(std::min(a, b), std::min(std::max(a, b), c)); });
        lua.set_function("sgn", [](double x) { return x < 0.0 ? -1.0 : 1.0; });
        lua.set_function("sqrt", [](double x) { return std::sqrt(x); });
        lua.set_function("sin", [](double x) { return std::sin(x); });
        lua.set_function("cos", [](double x) { return std::cos(x); });
        lua.set_function("atan2", [](double y, double x) { return std::atan2(y, x); });
        lua.set_function("rnd",
                         [](sol::optional<double> x)
                         {
                             std::uniform_real_distribution<double> d(0.0, x.value_or(1.0));
                             return d(rng());
                         });
        lua.set_function("srand", [](sol::optional<double> s) { rng().seed(static_cast<unsigned>(s.value_or(0.0))); });
        lua.set_function("t", []() { return now_seconds(); });
        lua.set_function("time", []() { return now_seconds(); });
    }
} // namespace lazy100
