#include "lazy100/video/sprites.hpp"

#include "lazy100/video/framebuffer.hpp"

namespace lazy100
{
    u8 SpriteSheet::get(int x, int y) const
    {
        return in_bounds(x, y) ? px_[static_cast<u32>(y) * kSize + static_cast<u32>(x)] : 0;
    }

    void SpriteSheet::set(int x, int y, u8 index)
    {
        if (in_bounds(x, y))
            px_[static_cast<u32>(y) * kSize + static_cast<u32>(x)] = index;
    }

    void SpriteSheet::clear()
    {
        px_.fill(0);
        flags_.fill(0);
    }

    u8 SpriteSheet::flags(int n) const { return (n >= 0 && n < kSpriteCount) ? flags_[n] : 0; }
    void SpriteSheet::set_flags(int n, u8 f)
    {
        if (n >= 0 && n < kSpriteCount)
            flags_[n] = f;
    }
    bool SpriteSheet::flag_bit(int n, int bit) const
    {
        return bit >= 0 && bit < 8 && (flags(n) & (1u << bit));
    }
    void SpriteSheet::set_flag_bit(int n, int bit, bool on)
    {
        if (n < 0 || n >= kSpriteCount || bit < 0 || bit >= 8)
            return;
        if (on)
            flags_[n] |= static_cast<u8>(1u << bit);
        else
            flags_[n] &= static_cast<u8>(~(1u << bit));
    }

    void SpriteSheet::spr(Framebuffer& fb, int n, int x, int y, int w, int h, bool fx, bool fy, const u8* draw_pal,
                          const bool* transp) const
    {
        if (n < 0)
            return;
        const int ox = (n % kSpritesPerRow) * 8;
        const int oy = (n / kSpritesPerRow) * 8;
        const int sw = w * 8;
        const int sh = h * 8;
        for (int j = 0; j < sh; ++j)
            for (int i = 0; i < sw; ++i)
            {
                const int sxx = fx ? (sw - 1 - i) : i;
                const int syy = fy ? (sh - 1 - j) : j;
                const u8  c   = get(ox + sxx, oy + syy);
                if (transp[c])
                    continue;
                fb.pset(x + i, y + j, draw_pal[c]);
            }
    }

    void SpriteSheet::sspr(Framebuffer& fb, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool fx,
                           bool fy, const u8* draw_pal, const bool* transp) const
    {
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
            return;
        for (int j = 0; j < dh; ++j)
            for (int i = 0; i < dw; ++i)
            {
                int u = sw * i / dw; // nearest-neighbor scale dst -> src
                int v = sh * j / dh;
                if (fx)
                    u = sw - 1 - u;
                if (fy)
                    v = sh - 1 - v;
                const u8 c = get(sx + u, sy + v);
                if (transp[c])
                    continue;
                fb.pset(dx + i, dy + j, draw_pal[c]);
            }
    }
} // namespace lazy100
