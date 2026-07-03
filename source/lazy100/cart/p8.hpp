#pragma once

#include <string>
#include <vector>

namespace lazy100
{
    class SpriteSheet;
    class Map;
    struct SoundBank;

    // The .p8 ext compatibility layer: imports carts in the classic .p8 text format or the
    // shareable 160x205 .p8.png format, converting them into native Lazy-100 assets plus a
    // self-contained Lua program (runtime shim + dialect-translated code). The imported cart
    // runs inside a centered 128x128 viewport with a badge frame, and can be saved back out
    // as a regular .lz100.
    namespace p8
    {
        // Quick sniffs. is_png_cart decodes just enough of the file to check the 160x205 shape.
        bool is_text_cart(const std::string& text);
        bool is_png_cart(const std::string& path);

        // Import either form into the console's structures. `code` receives the full runnable
        // program (prelude + translated cart code + epilogue). False on parse/decode failure.
        // `romOut`, when given, receives the 32KB cart ROM image - the runtime keeps it as the
        // cart's addressable RAM so peek/poke (and carts that retrack audio memory) work.
        // `rawOut`, when given, receives the cart's untranslated Lua source (the __lua__
        // section) - the native z8lua VM runs the p8 dialect directly, no translation.
        bool import_text(const std::string& text, std::string& code, SpriteSheet& sheet, Map& map,
                         SoundBank& sounds, std::vector<unsigned char>* romOut = nullptr,
                         std::string* rawOut = nullptr);
        bool import_png(const std::string& path, std::string& code, SpriteSheet& sheet, Map& map,
                        SoundBank& sounds, std::vector<unsigned char>* romOut = nullptr,
                        std::string* rawOut = nullptr);

        // Decode the audio region of p8 RAM (`ram` points at address 0x3100: the music table,
        // with the sfx table at +0x100) into a SoundBank. The runtime calls this again whenever
        // a cart that pokes audio memory starts a sound, so live edits are heard.
        void decode_audio_ram(const unsigned char* ram, SoundBank& sounds);
    } // namespace p8
} // namespace lazy100
