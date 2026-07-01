#pragma once

#include "lazy100/common/types.hpp"

#include <array>

namespace lazy100
{
    // The cart's thumbnail: a full-resolution 320x240 indexed snapshot of the screen, captured
    // from a running cart with Ctrl+7 and stored in the cart's __label__ section. Used as the
    // visible picture when exporting a cart PNG (kept at full res so it stays crisp).
    struct CartLabel
    {
        static constexpr int kW = 320;
        static constexpr int kH = 240;

        std::array<u8, static_cast<size_t>(kW) * kH> px {};
        bool                                         present = false; // false => no label captured yet

        void clear()
        {
            px      = {};
            present = false;
        }
    };
} // namespace lazy100
