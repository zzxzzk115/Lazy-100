#include "lazy100/cart/cart.hpp"

#include "lazy100/audio/sound.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <sstream>
#include <vector>

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

        // Decode a run of hex chars into bytes (ignores stray non-hex).
        std::vector<u8> hex_to_bytes(const std::string& hex)
        {
            std::vector<u8> out;
            out.reserve(hex.size() / 2);
            for (size_t i = 0; i + 1 < hex.size(); i += 2)
            {
                const int hi = hex_val(hex[i]);
                const int lo = hex_val(hex[i + 1]);
                if (hi >= 0 && lo >= 0)
                    out.push_back(static_cast<u8>((hi << 4) | lo));
            }
            return out;
        }

        void put_byte(std::string& out, u8 v)
        {
            out += hex_digit(v >> 4);
            out += hex_digit(v & 0xf);
        }
    } // namespace

    bool parse(const std::string& text, std::string& code, SpriteSheet& sheet, Map& map, SoundBank& bank)
    {
        code.clear();
        sheet.clear();
        map.clear();
        bank.clear();

        std::string gfxHex, gffHex, mapHex, sfxHex, musicHex;
        bool        sawSection = false;

        enum class Sec
        {
            None,
            Lua,
            Gfx,
            Gff,
            Map,
            Sfx,
            Music
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
                sec        = name == "lua"     ? Sec::Lua
                             : name == "gfx"   ? Sec::Gfx
                             : name == "gff"   ? Sec::Gff
                             : name == "map"   ? Sec::Map
                             : name == "sfx"   ? Sec::Sfx
                             : name == "music" ? Sec::Music
                                               : Sec::None; // unknown sections skipped
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
                case Sec::Map: mapHex += line; break;
                case Sec::Sfx: sfxHex += line; break;
                case Sec::Music: musicHex += line; break;
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
        u8* tiles = map.tiles();
        for (size_t i = 0, t = 0; i + 1 < mapHex.size() && t < static_cast<size_t>(Map::kW) * Map::kH; i += 2, ++t)
        {
            const int hi = hex_val(mapHex[i]);
            const int lo = hex_val(mapHex[i + 1]);
            if (hi >= 0 && lo >= 0)
                tiles[t] = static_cast<u8>((hi << 4) | lo);
        }

        // __sfx__: 65 bytes per pattern (speed + 32 notes * 2 bytes: pitch, wave|vol<<3|effect<<6).
        {
            const std::vector<u8> b   = hex_to_bytes(sfxHex);
            constexpr size_t      rec = 1 + SfxPattern::kSteps * 2;
            for (size_t s = 0; s < SoundBank::kSfxCount && (s + 1) * rec <= b.size(); ++s)
            {
                SfxPattern&  pat  = bank.sfx[s];
                const u8*    r    = &b[s * rec];
                pat.speed         = r[0] ? r[0] : 1;
                for (int n = 0; n < SfxPattern::kSteps; ++n)
                {
                    const u8 pitch  = r[1 + n * 2];
                    const u8 packed = r[2 + n * 2];
                    pat.notes[n]    = {pitch, static_cast<u8>(packed & 0x7), static_cast<u8>((packed >> 3) & 0x7),
                                       static_cast<u8>((packed >> 6) & 0x3)};
                }
            }
        }
        // __music__: 5 bytes per pattern (flags + 4 channel sfx indices).
        {
            const std::vector<u8> b   = hex_to_bytes(musicHex);
            constexpr size_t      rec = 1 + MusicPattern::kChannels;
            for (size_t m = 0; m < SoundBank::kMusicCount && (m + 1) * rec <= b.size(); ++m)
            {
                MusicPattern& mp = bank.music[m];
                const u8*     r  = &b[m * rec];
                mp.flags         = r[0];
                for (int c = 0; c < MusicPattern::kChannels; ++c)
                    mp.sfx[c] = r[1 + c];
            }
        }
        return true;
    }

    std::string serialize(const std::string& code, const SpriteSheet& sheet, const Map& map, const SoundBank& bank)
    {
        std::string out;
        out.reserve(static_cast<size_t>(kSheet) * kSheet * 2 + static_cast<size_t>(Map::kW) * Map::kH * 2 +
                    code.size() + 256);
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

        out += "__map__\n";
        const u8* tiles = map.tiles();
        for (int y = 0; y < Map::kH; ++y)
        {
            for (int x = 0; x < Map::kW; ++x)
            {
                const u8 v = tiles[y * Map::kW + x];
                out += hex_digit(v >> 4);
                out += hex_digit(v & 0xf);
            }
            out += '\n';
        }

        out += "__sfx__\n";
        for (int s = 0; s < SoundBank::kSfxCount; ++s)
        {
            const SfxPattern& pat = bank.sfx[s];
            put_byte(out, pat.speed);
            for (const SfxNote& note : pat.notes)
            {
                put_byte(out, note.pitch);
                put_byte(out, static_cast<u8>((note.wave & 0x7) | ((note.vol & 0x7) << 3) | ((note.effect & 0x3) << 6)));
            }
            out += '\n';
        }

        out += "__music__\n";
        for (int m = 0; m < SoundBank::kMusicCount; ++m)
        {
            const MusicPattern& mp = bank.music[m];
            put_byte(out, mp.flags);
            for (u8 idx : mp.sfx)
                put_byte(out, idx);
            out += '\n';
        }
        return out;
    }
} // namespace lazy100::cart
