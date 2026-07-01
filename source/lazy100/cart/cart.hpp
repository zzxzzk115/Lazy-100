#pragma once

#include <string>

namespace lazy100
{
    class SpriteSheet;

    // The .lz100 cart format: a PICO-8-style single text file with sections.
    //   lazy-100 cartridge / version 1
    //   __lua__   <source>
    //   __gfx__   128 rows of 256 hex chars (128 px * 2 hex; palette index 00..1f)
    //   __gff__   256 sprite flags as hex bytes
    // (map/sfx/music sections arrive with M9/M11; unknown sections are skipped.)
    namespace cart
    {
        // Parse .lz100 text: fill `code` (__lua__) and restore `sheet` from __gfx__/__gff__.
        // A plain .lua file (no sections) is treated as code-only. Always returns a usable cart.
        bool parse(const std::string& text, std::string& code, SpriteSheet& sheet);

        // Serialize the current code + sprite sheet to .lz100 text.
        std::string serialize(const std::string& code, const SpriteSheet& sheet);
    } // namespace cart
} // namespace lazy100
