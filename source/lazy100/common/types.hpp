#pragma once

#include <cstdint>

namespace lazy100
{
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    // An unpacked RGBA8 color. The on-screen framebuffer stores palette indices (u8); a
    // Color32 is what each palette entry resolves to.
    struct Color32
    {
        u8 r = 0, g = 0, b = 0, a = 255;
    };
} // namespace lazy100
