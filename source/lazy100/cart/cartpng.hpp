#pragma once

#include "lazy100/cart/label.hpp"

#include <string>

namespace lazy100
{
    class SpriteSheet;
    class Palette;

    // A shareable "cartridge PNG": the image shows the sprite sheet as a label, and the whole
    // serialized .lz100 cart (RLE-compressed) is hidden in the low 2 bits of the RGBA pixels
    // (classic fantasy-console trick). Lossless as long as it stays a PNG.
    namespace cartpng
    {
        // Encode `cartText` (from cart::serialize) into a cart PNG at `path`. The visible picture
        // is `label` if one was captured, else `sheet` (colored via `pal`) as a fallback thumbnail.
        // Returns false on write failure.
        bool save(const std::string& path, const std::string& cartText, const CartLabel& label,
                  const SpriteSheet& sheet, const Palette& pal);

        // Recover the serialized cart text hidden in a cart PNG. False if it isn't one.
        bool load(const std::string& path, std::string& cartText);

        // Write a plain RGBA8 image (no hidden payload) - used by tools (cartshot previews).
        bool write_rgba(const std::string& path, int w, int h, const unsigned char* rgba);
    } // namespace cartpng
} // namespace lazy100
