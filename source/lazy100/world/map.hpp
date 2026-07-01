#pragma once

#include "lazy100/common/types.hpp"

#include <array>

namespace lazy100
{
    // The tile map: 128x64 cells, each a sprite index (0 = empty). Edited by the map editor,
    // read by the Lua map()/mget/mset, serialized in the cart __map__ section.
    class Map
    {
    public:
        static constexpr int kW = 128;
        static constexpr int kH = 64;

        u8   get(int x, int y) const
        {
            return in_bounds(x, y) ? tiles_[static_cast<u32>(y) * kW + static_cast<u32>(x)] : 0;
        }
        void set(int x, int y, u8 v)
        {
            if (in_bounds(x, y))
                tiles_[static_cast<u32>(y) * kW + static_cast<u32>(x)] = v;
        }
        void clear() { tiles_.fill(0); }

        u8*       tiles() { return tiles_.data(); }
        const u8* tiles() const { return tiles_.data(); }

    private:
        static bool in_bounds(int x, int y) { return x >= 0 && y >= 0 && x < kW && y < kH; }

        std::array<u8, kW * kH> tiles_ {};
    };
} // namespace lazy100
