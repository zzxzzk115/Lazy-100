#pragma once

#include <string>

namespace lazy100
{
    class SpriteSheet;
    class Map;
    struct SoundBank;

    // The .lz100 cart format: a PICO-8-style single text file with sections.
    //   lazy-100 cartridge / version 1
    //   __lua__   <source>
    //   __gfx__   256 rows of 512 hex chars (256 px * 2 hex; palette index 00..ff)
    //   __gff__   256 sprite flags as hex bytes
    //   __map__   64 rows of 256 hex chars (128 tiles * 2 hex; sprite index 00..ff)
    //   __sfx__   64 rows of 130 hex chars (speed byte + 32 notes * 2 bytes)
    //   __music__ 64 rows of 10 hex chars (flags byte + 4 channel sfx indices)
    // (unknown sections are skipped.)
    namespace cart
    {
        // Parse .lz100 text: fill `code` (__lua__) and restore `sheet` (__gfx__/__gff__),
        // `map` (__map__), `bank` (__sfx__/__music__). A plain .lua file (no sections) is
        // treated as code-only. Always returns a usable cart.
        bool parse(const std::string& text, std::string& code, SpriteSheet& sheet, Map& map, SoundBank& bank);

        // Serialize the current code + sprite sheet + map + sound bank to .lz100 text.
        std::string serialize(const std::string& code, const SpriteSheet& sheet, const Map& map,
                              const SoundBank& bank);
    } // namespace cart
} // namespace lazy100
