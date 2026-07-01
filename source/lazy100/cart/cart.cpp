#include "lazy100/cart/cart.hpp"

#include "lazy100/video/sprites.hpp"

#include <sstream>

namespace lazy100::cart
{
    namespace
    {
        constexpr int kSheet = SpriteSheet::kSize;        // 256 (px per side)
        constexpr int kFlags = SpriteSheet::kSpriteCount; // 256 sprites

        char hex_digit(int v) { return static_cast<char>(v < 10 ? '0' + v : 'a' + (v - 10)); }

        int hex_val(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        // Section marker line "__name__" -> name; empty if not a marker.
        std::string section_name(const std::string& line)
        {
            if (line.size() >= 5 && line.compare(0, 2, "__") == 0 &&
                line.compare(line.size() - 2, 2, "__") == 0)
                return line.substr(2, line.size() - 4);
            return {};
        }
    } // namespace

    bool parse(const std::string& text, std::string& code, SpriteSheet& sheet)
    {
        code.clear();
        sheet.clear();

        std::string gfxHex, gffHex;
        bool        sawSection = false;

        enum class Sec
        {
            None,
            Lua,
            Gfx,
            Gff
        } sec = Sec::None;

        std::istringstream ss(text);
        std::string        line;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const std::string name = section_name(line);
            if (!name.empty())
            {
                sawSection = true;
                sec        = name == "lua"   ? Sec::Lua
                             : name == "gfx" ? Sec::Gfx
                             : name == "gff" ? Sec::Gff
                                             : Sec::None; // map/sfx/music etc. skipped for now
                continue;
            }

            switch (sec)
            {
                case Sec::Lua:
                    code += line;
                    code += '\n';
                    break;
                case Sec::Gfx: gfxHex += line; break;
                case Sec::Gff: gffHex += line; break;
                case Sec::None: break; // header lines / unknown sections
            }
        }

        // A plain .lua file (no __lua__ marker) is a code-only cart.
        if (!sawSection)
            code = text;

        u8* px = sheet.pixels();
        for (size_t i = 0, p = 0; i + 1 < gfxHex.size() && p < static_cast<size_t>(kSheet) * kSheet; i += 2, ++p)
        {
            const int hi = hex_val(gfxHex[i]);
            const int lo = hex_val(gfxHex[i + 1]);
            if (hi >= 0 && lo >= 0)
                px[p] = static_cast<u8>((hi << 4) | lo);
        }
        for (size_t i = 0, n = 0; i + 1 < gffHex.size() && n < static_cast<size_t>(kFlags); i += 2, ++n)
        {
            const int hi = hex_val(gffHex[i]);
            const int lo = hex_val(gffHex[i + 1]);
            if (hi >= 0 && lo >= 0)
                sheet.set_flags(static_cast<int>(n), static_cast<u8>((hi << 4) | lo));
        }
        return true;
    }

    std::string serialize(const std::string& code, const SpriteSheet& sheet)
    {
        std::string out;
        out.reserve(static_cast<size_t>(kSheet) * kSheet * 2 + code.size() + 256);
        out += "lazy-100 cartridge\nversion 1\n";

        out += "__lua__\n";
        out += code;
        if (code.empty() || code.back() != '\n')
            out += '\n';

        out += "__gfx__\n";
        const u8* px = sheet.pixels();
        for (int y = 0; y < kSheet; ++y)
        {
            for (int x = 0; x < kSheet; ++x)
            {
                const u8 v = px[y * kSheet + x];
                out += hex_digit(v >> 4);
                out += hex_digit(v & 0xf);
            }
            out += '\n';
        }

        out += "__gff__\n";
        for (int n = 0; n < kFlags; ++n)
        {
            const u8 f = sheet.flags(n);
            out += hex_digit(f >> 4);
            out += hex_digit(f & 0xf);
            if ((n % 16) == 15)
                out += '\n';
        }
        return out;
    }
} // namespace lazy100::cart
