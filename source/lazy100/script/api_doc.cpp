#include "lazy100/script/api_doc.hpp"

#include <iterator>
#include <unordered_map>

namespace lazy100::apidoc
{
    namespace
    {
        // clang-format off
        constexpr Fn kCallbacks[] = {
            {"_init",     "_init()",     "called once when the cart starts"},
            {"_update",   "_update()",   "game logic at 30 fps"},
            {"_update60", "_update60()", "define instead of _update for 60 fps"},
            {"_draw",     "_draw()",     "render one frame (after update)"},
        };
        constexpr Fn kGraphics[] = {
            {"cls",      "cls([color])",                          "clear the screen (default color 0)"},
            {"pset",     "pset(x, y, [color])",                   "set one pixel"},
            {"pget",     "pget(x, y)",                            "read one pixel's palette index"},
            {"line",     "line(x0, y0, x1, y1, [color])",         "line between two points"},
            {"rect",     "rect(x0, y0, x1, y1, [color])",         "rectangle outline"},
            {"rectfill", "rectfill(x0, y0, x1, y1, [color])",     "filled rectangle"},
            {"circ",     "circ(x, y, radius, [color])",           "circle outline"},
            {"circfill", "circfill(x, y, radius, [color])",       "filled circle"},
            {"oval",     "oval(x0, y0, x1, y1, [color])",         "ellipse outline in a bounding box"},
            {"ovalfill", "ovalfill(x0, y0, x1, y1, [color])",     "filled ellipse in a bounding box"},
            {"print",    "print(text, [x], [y], [color])",        "draw text; returns the end x"},
            {"camera",   "camera([x], [y])",                      "scroll offset for all drawing; no args resets"},
            {"clip",     "clip([x], [y], [w], [h])",              "restrict drawing to a rect; no args resets"},
        };
        constexpr Fn kSprites[] = {
            {"spr",  "spr(n, x, y, [w], [h], [flip_x], [flip_y])",                   "draw sprite n (w*h block of 16px cells)"},
            {"sspr", "sspr(sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])", "draw a sheet rect, scaled to dw*dh"},
            {"sget", "sget(x, y)",                                                   "read a sheet pixel"},
            {"sset", "sset(x, y, [color])",                                          "write a sheet pixel"},
            {"fget", "fget(n, [bit])",                                               "sprite flags (or one flag bit)"},
            {"fset", "fset(n, flags_or_bit, [on])",                                  "set sprite flags (or one flag bit)"},
        };
        constexpr Fn kMap[] = {
            {"mget", "mget(cel_x, cel_y)",                                        "tile at a map cell (255 = empty)"},
            {"mset", "mset(cel_x, cel_y, tile)",                                  "write a map cell"},
            {"map",  "map([cel_x], [cel_y], [scr_x], [scr_y], [cel_w], [cel_h])", "draw a map region as 16px tiles"},
        };
        constexpr Fn kPalette[] = {
            {"pal",  "pal([c0], [c1], [mode])", "remap color c0 -> c1 (mode 1 = screen); no args resets"},
            {"palt", "palt([color], [transparent])", "set a color's transparency; no args resets"},
        };
        constexpr Fn kAudio[] = {
            {"sfx",   "sfx(n, [channel])", "play sfx pattern n (channel -1 = auto)"},
            {"music", "music([n])",        "play music from pattern n; music(-1) stops"},
        };
        constexpr Fn kInput[] = {
            {"btn",  "btn([b], [player])",  "button held? no args = bitmask"},
            {"btnp", "btnp([b], [player])", "button pressed (with auto-repeat)?"},
            {"stat", "stat(n)",             "console state: 32/33 mouse x/y, 34 buttons"},
        };
        constexpr Fn kMath[] = {
            {"flr",   "flr(x)",         "round down to an integer"},
            {"ceil",  "ceil(x)",        "round up to an integer"},
            {"abs",   "abs(x)",         "absolute value"},
            {"min",   "min(a, b)",      "smaller of two values"},
            {"max",   "max(a, b)",      "larger of two values"},
            {"mid",   "mid(a, b, c)",   "middle of three values (clamp)"},
            {"sgn",   "sgn(x)",         "-1 or 1 (0 counts as positive)"},
            {"sqrt",  "sqrt(x)",        "square root"},
            {"sin",   "sin(turns)",     "sine; angle in turns (1.0 = full circle), inverted y"},
            {"cos",   "cos(turns)",     "cosine; angle in turns (1.0 = full circle)"},
            {"atan2", "atan2(dx, dy)",  "angle of a vector, in turns 0..1"},
            {"rnd",   "rnd([max_or_table])", "random [0,max), or a random element of a table"},
            {"srand", "srand([seed])",  "seed the random generator"},
        };
        constexpr Fn kTime[] = {
            {"t",    "t()",    "seconds since the cart started"},
            {"time", "time()", "alias of t()"},
        };
        constexpr Fn kSave[] = {
            {"cartdata", "cartdata(id)",     "open the cart's 64-slot save file (saves/<id>)"},
            {"dset",     "dset(index, value)", "store a number in slot 0..63 (persisted)"},
            {"dget",     "dget(index)",        "read a saved slot (0 if unset)"},
        };
        constexpr Fn kTables[] = {
            {"add",     "add(t, value, [index])", "append (or insert) into a table; returns value"},
            {"del",     "del(t, value)",          "remove the first matching value"},
            {"deli",    "deli(t, [index])",       "remove by index (default: last)"},
            {"count",   "count(t, [value])",      "#t, or how many equal value"},
            {"all",     "all(t)",                 "iterator: for item in all(t) do ... end"},
            {"foreach", "foreach(t, fn)",         "call fn(item) for every item"},
        };
        constexpr Fn kStrings[] = {
            {"tostr", "tostr(value, [hex])",         "value -> string (hex formats numbers)"},
            {"tonum", "tonum(s)",                    "string -> number, or nil"},
            {"chr",   "chr(code, ...)",              "character(s) from byte codes"},
            {"ord",   "ord(s, [index])",             "byte code of a character"},
            {"sub",   "sub(s, from, [to])",          "substring (1-based, inclusive)"},
            {"split", "split(s, [sep], [to_number])", "split into a table (default sep \",\")"},
        };
        constexpr Fn kCoroutines[] = {
            {"cocreate", "cocreate(fn)",       "create a coroutine"},
            {"coresume", "coresume(co, ...)",  "run/continue it; returns ok, ..."},
            {"costatus", "costatus(co)",       "\"suspended\", \"running\" or \"dead\""},
            {"yield",    "yield(...)",         "pause the coroutine, handing values back"},
        };

        constexpr Category kCategories[] = {
            {"callbacks",  kCallbacks,  static_cast<int>(std::size(kCallbacks))},
            {"graphics",   kGraphics,   static_cast<int>(std::size(kGraphics))},
            {"sprites",    kSprites,    static_cast<int>(std::size(kSprites))},
            {"map",        kMap,        static_cast<int>(std::size(kMap))},
            {"palette",    kPalette,    static_cast<int>(std::size(kPalette))},
            {"audio",      kAudio,      static_cast<int>(std::size(kAudio))},
            {"input",      kInput,      static_cast<int>(std::size(kInput))},
            {"math",       kMath,       static_cast<int>(std::size(kMath))},
            {"time",       kTime,       static_cast<int>(std::size(kTime))},
            {"save data",  kSave,       static_cast<int>(std::size(kSave))},
            {"tables",     kTables,     static_cast<int>(std::size(kTables))},
            {"strings",    kStrings,    static_cast<int>(std::size(kStrings))},
            {"coroutines", kCoroutines, static_cast<int>(std::size(kCoroutines))},
        };
        // clang-format on
    } // namespace

    const Category* categories(int& count)
    {
        count = static_cast<int>(std::size(kCategories));
        return kCategories;
    }

    const Fn* find(const std::string& name)
    {
        static const std::unordered_map<std::string, const Fn*> index = []
        {
            std::unordered_map<std::string, const Fn*> m;
            for (const Category& cat : kCategories)
                for (int i = 0; i < cat.count; ++i)
                    m.emplace(cat.fns[i].name, &cat.fns[i]);
            return m;
        }();
        const auto it = index.find(name);
        return it == index.end() ? nullptr : it->second;
    }
} // namespace lazy100::apidoc
