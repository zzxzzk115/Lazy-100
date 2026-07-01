// Verifies that a cart survives a PNG export -> import round-trip byte-for-byte, and that the
// recovered text still reparses into the same code + sprite sheet.
#include "lazy100/audio/sound.hpp"
#include "lazy100/cart/cart.hpp"
#include "lazy100/cart/cartpng.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <cstdio>
#include <string>

using namespace lazy100;

int main()
{
    SpriteSheet sheet;
    Map         map;
    SoundBank   bank;
    Palette     pal;
    CartLabel   label;

    // Some representative content: UTF-8 source, scattered sprite pixels, a couple of sfx notes.
    const std::string code = "function _init()\n  x=1  -- 中文注释 with symbols: \"quotes\" & \\n\nend\n";
    sheet.set(3, 5, 12);
    sheet.set(200, 200, 8);
    sheet.set(255, 255, 15);
    bank.sfx[1].notes[0] = SfxNote {36, 2, 5, 0};
    bank.music[0].sfx[0] = 1;
    label.present   = true;
    label.px[0]     = 7;
    label.px[100]   = 12;
    label.px.back() = 8;

    const std::string text = cart::serialize(code, sheet, map, bank, label);

    const char* path = "cartpng_test.png";
    if (!cartpng::save(path, text, label, sheet, pal))
    {
        std::printf("FAIL: save\n");
        return 1;
    }

    std::string back;
    if (!cartpng::load(path, back))
    {
        std::printf("FAIL: load\n");
        return 2;
    }
    if (back != text)
    {
        std::printf("FAIL: payload mismatch (%zu vs %zu bytes)\n", text.size(), back.size());
        return 3;
    }

    // The recovered text must reparse into the same cart.
    std::string code2;
    SpriteSheet sheet2;
    Map         map2;
    SoundBank   bank2;
    CartLabel   label2;
    cart::parse(back, code2, sheet2, map2, bank2, label2);
    if (code2 != code)
    {
        std::printf("FAIL: code mismatch\n");
        return 4;
    }
    if (sheet2.get(3, 5) != 12 || sheet2.get(200, 200) != 8 || sheet2.get(255, 255) != 15)
    {
        std::printf("FAIL: sprite sheet mismatch\n");
        return 5;
    }
    if (bank2.sfx[1].notes[0].pitch != 36 || bank2.music[0].sfx[0] != 1)
    {
        std::printf("FAIL: sound bank mismatch\n");
        return 6;
    }
    if (!label2.present || label2.px[0] != 7 || label2.px[100] != 12 || label2.px.back() != 8)
    {
        std::printf("FAIL: label mismatch\n");
        return 7;
    }

    std::printf("OK: %zu-byte cart round-tripped through PNG; code+sheet+sound intact\n", text.size());
    std::remove(path);
    return 0;
}
