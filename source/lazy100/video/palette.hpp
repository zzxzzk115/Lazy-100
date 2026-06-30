#pragma once

#include "lazy100/common/types.hpp"
#include "lazy100/console/config.hpp"

#include <array>

namespace lazy100
{
    // The console's color table: kPaletteSize RGBA8 entries. The framebuffer stores indices
    // into this; the GPU resolves index -> color in the present shader, so palette tricks
    // (swaps, cycling, fades) are a tiny uniform update instead of a full-screen rewrite.
    class Palette
    {
    public:
        Palette() { reset(); }

        void    set(u32 index, Color32 c);
        Color32 get(u32 index) const { return colors_[index < kPaletteSize ? index : 0]; }
        void    reset();              // restore the built-in default palette
        Color32 default_at(u32 index) const; // the built-in default color for an index

        bool dirty() const { return dirty_; }
        void clear_dirty() { dirty_ = false; }

        // kPaletteSize entries packed little-endian as r | g<<8 | b<<16 | a<<24, for direct
        // upload into the present shader's palette uniform.
        const u32* packed() const { return packed_.data(); }

        static constexpr u32 size() { return kPaletteSize; }

    private:
        void repack();

        std::array<Color32, kPaletteSize> colors_ {};
        std::array<u32, kPaletteSize>     packed_ {};
        bool                              dirty_ = true;
    };
} // namespace lazy100
