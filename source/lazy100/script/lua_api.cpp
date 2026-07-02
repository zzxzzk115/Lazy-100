#include "lazy100/script/lua_api.hpp"

#include "lazy100/audio/audio.hpp"
#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/input/input.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

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

        // Lua numbers are doubles; the console floors coordinates so sol2
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
        Map&         world = console.map();
        Palette&     pal   = console.palette();
        u8*          dpal  = console.draw_pal();      // live draw-palette remap
        bool*        trans = console.transparent();   // live transparency mask

        // Shape pen: resolve a color arg, then apply the draw-palette remap.
        auto pen = [dpal](sol::optional<double> c, u8 def) -> u8 { return dpal[col(c, def)]; };

        // camera() scroll offset: world coordinate -> screen coordinate for every draw call.
        auto camx = [&console](double v) { return fi(v) - console.cam_x(); };
        auto camy = [&console](double v) { return fi(v) - console.cam_y(); };

        // ---- graphics (coords floored; pen remapped by pal()) ----
        lua.set_function("cls", [&fb](sol::optional<double> c) { fb.cls(col(c, 0)); });
        lua.set_function("pset",
                         [&fb, pen, camx, camy](double x, double y, sol::optional<double> c)
                         { fb.pset(camx(x), camy(y), pen(c, 7)); });
        lua.set_function("pget", [&fb, camx, camy](double x, double y)
                         { return static_cast<int>(fb.pget(camx(x), camy(y))); });
        lua.set_function("rectfill",
                         [&fb, pen, camx, camy](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { fb.rectfill(camx(x0), camy(y0), camx(x1), camy(y1), pen(c, 7)); });
        lua.set_function("rect",
                         [&fb, pen, camx, camy](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { draw::rect(fb, camx(x0), camy(y0), camx(x1), camy(y1), pen(c, 7)); });
        lua.set_function("line",
                         [&fb, pen, camx, camy](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { draw::line(fb, camx(x0), camy(y0), camx(x1), camy(y1), pen(c, 7)); });
        lua.set_function("circ",
                         [&fb, pen, camx, camy](double x, double y, double r, sol::optional<double> c)
                         { draw::circ(fb, camx(x), camy(y), fi(r), pen(c, 7)); });
        lua.set_function("circfill",
                         [&fb, pen, camx, camy](double x, double y, double r, sol::optional<double> c)
                         { draw::circfill(fb, camx(x), camy(y), fi(r), pen(c, 7)); });
        lua.set_function("oval",
                         [&fb, pen, camx, camy](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { draw::oval(fb, camx(x0), camy(y0), camx(x1), camy(y1), pen(c, 7)); });
        lua.set_function("ovalfill",
                         [&fb, pen, camx, camy](double x0, double y0, double x1, double y1, sol::optional<double> c)
                         { draw::ovalfill(fb, camx(x0), camy(y0), camx(x1), camy(y1), pen(c, 7)); });
        lua.set_function("print",
                         [&fb, pen, camx, camy](sol::object text, sol::optional<double> x, sol::optional<double> y,
                                                sol::optional<double> c)
                         {
                             return font::print(fb, to_text(text).c_str(), x ? camx(*x) : 0, y ? camy(*y) : 0,
                                                pen(c, 7));
                         });

        // camera([x, y]): set the draw offset; no args resets it.
        lua.set_function("camera",
                         [&console](sol::optional<double> x, sol::optional<double> y)
                         { console.set_camera(x ? fi(*x) : 0, y ? fi(*y) : 0); });
        // clip([x, y, w, h]): restrict drawing to a screen-space rectangle; no args resets it.
        lua.set_function("clip",
                         [&fb](sol::optional<double> x, sol::optional<double> y, sol::optional<double> w,
                               sol::optional<double> h)
                         {
                             if (x && y && w && h)
                                 fb.clip(fi(*x), fi(*y), fi(*w), fi(*h));
                             else
                                 fb.clip_reset();
                         });

        // ---- sprites ----
        lua.set_function("spr",
                         [&sheet, &fb, dpal, trans, camx, camy](double n, double x, double y, sol::optional<double> w,
                                                                sol::optional<double> h, sol::optional<bool> fx,
                                                                sol::optional<bool> fy)
                         {
                             sheet.spr(fb, fi(n), camx(x), camy(y), fi(w, 1), fi(h, 1), fx.value_or(false),
                                       fy.value_or(false), dpal, trans);
                         });
        lua.set_function("sspr",
                         [&sheet, &fb, dpal, trans, camx, camy](double sx, double sy, double sw, double sh, double dx,
                                                                double dy, sol::optional<double> dw,
                                                                sol::optional<double> dh, sol::optional<bool> fx,
                                                                sol::optional<bool> fy)
                         {
                             sheet.sspr(fb, fi(sx), fi(sy), fi(sw), fi(sh), camx(dx), camy(dy), fi(dw, fi(sw)),
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

        // ---- map: mget/mset read/write tiles; map() blits a region of 16px tiles ----
        lua.set_function("mget",
                         [&world](double x, double y) { return static_cast<int>(world.get(fi(x), fi(y))); });
        lua.set_function("mset",
                         [&world](double x, double y, sol::optional<double> v)
                         { world.set(fi(x), fi(y), static_cast<u8>(fi(v, 0) & 0xff)); });
        lua.set_function(
            "map",
            [&world, &sheet, &fb, dpal, trans, camx, camy](sol::optional<double> cx, sol::optional<double> cy,
                                                           sol::optional<double> sx, sol::optional<double> sy,
                                                           sol::optional<double> cw, sol::optional<double> ch)
            {
                const int cel_x = fi(cx, 0), cel_y = fi(cy, 0);
                const int scr_x = sx ? camx(*sx) : camx(0.0), scr_y = sy ? camy(*sy) : camy(0.0);
                const int cel_w = fi(cw, Map::kW), cel_h = fi(ch, Map::kH);
                for (int j = 0; j < cel_h; ++j)
                    for (int i = 0; i < cel_w; ++i)
                    {
                        const u8 n = world.get(cel_x + i, cel_y + j);
                        if (n == Map::kEmpty)
                            continue; // 255 is the empty cell
                        sheet.spr(fb, n, scr_x + i * SpriteSheet::kSpriteSize, scr_y + j * SpriteSheet::kSpriteSize,
                                  1, 1, false, false, dpal, trans);
                    }
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

        // ---- audio: sfx(n[,chan]) plays cart pattern n; music(n)/music(-1) start/stop ----
        Audio&     audio = console.audio();
        SoundBank& bank  = console.sounds();
        lua.set_function("sfx",
                         [&audio, &bank](double n, sol::optional<double> chan, sol::optional<double> /*off*/)
                         {
                             const int i = fi(n);
                             if (i >= 0 && i < SoundBank::kSfxCount)
                                 audio.play_sfx(bank.sfx[i], fi(chan, -1));
                         });
        lua.set_function("music",
                         [&audio, &bank, &console](sol::optional<double> n, sol::optional<double>,
                                                   sol::optional<double>)
                         {
                             const int i = fi(n, 0);
                             if (i < 0)
                                 audio.stop_music();
                             else if (i < SoundBank::kMusicCount)
                                 audio.play_music(i, console.sounds());
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
        // Trig works in turns (0..1 = one full revolution) with the screen's y axis pointing
        // down: sin is negated, and atan2(dx, dy) returns the vector's angle in turns,
        // counter-clockwise from east. sin(0.25) == -1 (up), atan2(0, 1) == 0.75 (down).
        constexpr double kTau = 6.28318530717958647692;
        lua.set_function("sin", [kTau](double x) { return -std::sin(x * kTau); });
        lua.set_function("cos", [kTau](double x) { return std::cos(x * kTau); });
        lua.set_function("atan2",
                         [kTau](double dx, double dy)
                         {
                             double a = std::atan2(-dy, dx) / kTau;
                             return a < 0.0 ? a + 1.0 : a;
                         });
        // rnd(n) -> [0, n); rnd() -> [0, 1); rnd(table) -> a random element.
        lua.set_function("rnd",
                         [](sol::this_state ts, sol::optional<sol::object> arg) -> sol::object
                         {
                             sol::state_view L(ts);
                             if (arg && arg->is<sol::table>())
                             {
                                 sol::table   t = arg->as<sol::table>();
                                 const size_t n = t.size();
                                 if (n == 0)
                                     return sol::make_object(L, sol::lua_nil);
                                 std::uniform_int_distribution<size_t> d(1, n);
                                 return t.get<sol::object>(d(rng()));
                             }
                             const double mx = (arg && arg->is<double>()) ? arg->as<double>() : 1.0;
                             std::uniform_real_distribution<double> d(0.0, mx);
                             return sol::make_object(L, d(rng()));
                         });
        lua.set_function("srand", [](sol::optional<double> s) { rng().seed(static_cast<unsigned>(s.value_or(0.0))); });
        lua.set_function("t", []() { return now_seconds(); });
        lua.set_function("time", []() { return now_seconds(); });

        // ---- cart save data (cartdata/dset/dget) ----
        // cartdata(id) opens the cart's 64-number save slot (persisted under saves/<id>.txt);
        // dset writes through immediately, dget reads 0 for unset slots or before cartdata().
        lua.set_function("cartdata",
                         [&console](const std::string& id) { return console.cartdata_open(id); });
        lua.set_function("dget", [&console](double i) { return console.cartdata_get(fi(i)); });
        lua.set_function("dset", [&console](double i, double v) { console.cartdata_set(fi(i), v); });

        // ---- stat(n): console state queries (devkit mouse; unknown ids read 0) ----
        lua.set_function("stat",
                         [&console](double n) -> double
                         {
                             switch (fi(n))
                             {
                                 case 32: return console.mouse().x();
                                 case 33: return console.mouse().y();
                                 case 34: // button bitfield: 1 left, 2 right, 4 middle
                                     return (console.mouse().down(Mouse::Left) ? 1.0 : 0.0) +
                                            (console.mouse().down(Mouse::Right) ? 2.0 : 0.0) +
                                            (console.mouse().down(Mouse::Middle) ? 4.0 : 0.0);
                                 default: return 0.0;
                             }
                         });

        // ---- table/string helpers + coroutine aliases carts expect, and harmless stubs so
        // ported code doesn't crash on features the console doesn't model (raw memory, pause
        // menu, manual flip). Registered as plain Lua for natural semantics. ----
        lua.script(R"lua(
            function add(t, v, pos)
                if t == nil then return end
                if pos then table.insert(t, pos, v) else t[#t + 1] = v end
                return v
            end
            function del(t, v)
                if t == nil then return end
                for i = 1, #t do
                    if t[i] == v then table.remove(t, i) return v end
                end
            end
            function deli(t, i)
                if t == nil then return end
                if i == nil then return table.remove(t) end
                return table.remove(t, i)
            end
            function count(t, v)
                if t == nil then return 0 end
                if v == nil then return #t end
                local n = 0
                for i = 1, #t do if t[i] == v then n = n + 1 end end
                return n
            end
            function all(t)
                local i = 0
                return function()
                    if t == nil then return nil end
                    i = i + 1
                    return t[i]
                end
            end
            function foreach(t, f) for v in all(t) do f(v) end end

            sub = string.sub
            function tostr(v, hex)
                if hex and type(v) == "number" then return string.format("0x%x", math.floor(v)) end
                if v == nil then return "[nil]" end
                return tostring(v)
            end
            function tonum(v) return tonumber(v) end
            function chr(...)
                local s = ""
                for _, n in ipairs({...}) do s = s .. string.char(math.floor(n) % 256) end
                return s
            end
            function ord(s, i) if s == nil then return nil end return string.byte(s, i or 1) end
            function split(s, sep, conv)
                if s == nil then return {} end
                sep = sep or ","
                if conv == nil then conv = true end
                local out = {}
                if sep == "" then
                    for i = 1, #s do
                        local c = string.sub(s, i, i)
                        out[#out + 1] = (conv and tonumber(c)) or c
                    end
                    return out
                end
                local start = 1
                while true do
                    local pos = string.find(s, sep, start, true)
                    local piece = pos and string.sub(s, start, pos - 1) or string.sub(s, start)
                    out[#out + 1] = (conv and tonumber(piece)) or piece
                    if not pos then break end
                    start = pos + #sep
                end
                return out
            end

            cocreate, coresume, costatus, yield =
                coroutine.create, coroutine.resume, coroutine.status, coroutine.yield

            function menuitem() end
            function flip() end
            function peek() return 0 end
            function poke() end
            peek2, poke2, peek4, poke4 = peek, poke, peek, poke
            function memcpy() end
            function memset() end
            function reload() end
            function cstore() end
            function extcmd() end
        )lua");
    }
} // namespace lazy100
