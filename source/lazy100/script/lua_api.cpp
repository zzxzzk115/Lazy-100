#include "lazy100/script/lua_api.hpp"

#include "lazy100/console/config.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"

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

        // Resolve a Lua color arg to a palette index, wrapping into range; default when omitted.
        u8 col(sol::optional<int> c, u8 def)
        {
            return c ? static_cast<u8>(static_cast<u32>(*c) & (kPaletteSize - 1)) : def;
        }

        // Stringify a print() argument the way a fantasy console expects (numbers without a
        // trailing ".0", booleans as words).
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
        Framebuffer& fb = console.framebuffer();

        // ---- graphics (color = palette index; default pen is 7/white, cls is 0/black) ----
        lua.set_function("cls", [&fb](sol::optional<int> c) { fb.cls(col(c, 0)); });
        lua.set_function("pset", [&fb](int x, int y, sol::optional<int> c) { fb.pset(x, y, col(c, 7)); });
        lua.set_function("pget", [&fb](int x, int y) { return static_cast<int>(fb.pget(x, y)); });
        lua.set_function("rectfill",
                         [&fb](int x0, int y0, int x1, int y1, sol::optional<int> c)
                         { fb.rectfill(x0, y0, x1, y1, col(c, 7)); });
        lua.set_function("rect",
                         [&fb](int x0, int y0, int x1, int y1, sol::optional<int> c)
                         { draw::rect(fb, x0, y0, x1, y1, col(c, 7)); });
        lua.set_function("line",
                         [&fb](int x0, int y0, int x1, int y1, sol::optional<int> c)
                         { draw::line(fb, x0, y0, x1, y1, col(c, 7)); });
        lua.set_function("circ", [&fb](int x, int y, int r, sol::optional<int> c) { draw::circ(fb, x, y, r, col(c, 7)); });
        lua.set_function("circfill",
                         [&fb](int x, int y, int r, sol::optional<int> c) { draw::circfill(fb, x, y, r, col(c, 7)); });

        // ---- text ----
        lua.set_function("print",
                         [&fb](sol::object text, sol::optional<int> x, sol::optional<int> y, sol::optional<int> c)
                         { return font::print(fb, to_text(text).c_str(), x.value_or(0), y.value_or(0), col(c, 7)); });

        // ---- math helpers carts expect ----
        lua.set_function("flr", [](double x) { return std::floor(x); });
        lua.set_function("ceil", [](double x) { return std::ceil(x); });
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
