#include "lazy100/cart/cartpng.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"
#include "lazy100/video/sprites.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lazy100::cartpng
{
    namespace
    {
        // Cartridge cover layout (widths/gaps around the fixed kShot* screenshot region).
        constexpr int     kW      = kShotW + 2 * kShotX; // 352: card width
        constexpr int     kHeader = 20;                  // header band height (matches kShotY math)
        constexpr int     kGap1   = 8;                   // header -> screenshot
        constexpr int     kGap2   = 10;                  // screenshot -> footer
        constexpr int     kBot    = 12;                  // bottom margin
        constexpr int     kSheet  = 256;                 // fallback sprite-sheet picture (no label)
        constexpr uint8_t kMagic[4] = {'L', 'Z', '1', 'P'};

        void put32(std::vector<uint8_t>& v, uint32_t x)
        {
            v.push_back(x & 0xFF);
            v.push_back((x >> 8) & 0xFF);
            v.push_back((x >> 16) & 0xFF);
            v.push_back((x >> 24) & 0xFF);
        }
        uint32_t get32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        // Escape-based RLE. A token 0x00,b,lo,hi means byte b repeated (lo|hi<<8) times; any other
        // byte is a literal. 0x00 bytes are always escaped, so the stream round-trips any input
        // (cart text is zero-run-heavy hex, which this crushes).
        std::vector<uint8_t> rle_encode(const std::string& s)
        {
            std::vector<uint8_t> out;
            const size_t         n = s.size();
            for (size_t i = 0; i < n;)
            {
                const uint8_t b = static_cast<uint8_t>(s[i]);
                size_t        j = i + 1;
                while (j < n && static_cast<uint8_t>(s[j]) == b)
                    ++j;
                size_t run = j - i;
                if (b == 0x00 || run >= 4)
                    while (run > 0)
                    {
                        const uint32_t c = run > 65535 ? 65535u : static_cast<uint32_t>(run);
                        out.push_back(0x00);
                        out.push_back(b);
                        out.push_back(c & 0xFF);
                        out.push_back((c >> 8) & 0xFF);
                        run -= c;
                    }
                else
                    for (size_t k = 0; k < run; ++k)
                        out.push_back(b);
                i = j;
            }
            return out;
        }
        std::string rle_decode(const uint8_t* p, size_t len, uint32_t rawLen)
        {
            std::string out;
            out.reserve(rawLen);
            for (size_t i = 0; i < len;)
            {
                const uint8_t c = p[i++];
                if (c == 0x00)
                {
                    if (i + 3 > len)
                        break;
                    const uint8_t  b   = p[i];
                    const uint32_t cnt = static_cast<uint32_t>(p[i + 1]) | (static_cast<uint32_t>(p[i + 2]) << 8);
                    i += 3;
                    out.append(cnt, static_cast<char>(b));
                }
                else
                    out.push_back(static_cast<char>(c));
            }
            return out;
        }
    } // namespace

    bool save(const std::string& path, const std::string& cartText, const CartLabel& label,
              const SpriteSheet& sheet, const Palette& pal, const std::string& title,
              const std::string& author)
    {
        // Payload = magic + rawLen + rleLen + RLE bytes.
        const std::vector<uint8_t> rle = rle_encode(cartText);
        std::vector<uint8_t>       payload;
        payload.insert(payload.end(), kMagic, kMagic + 4);
        put32(payload, static_cast<uint32_t>(cartText.size()));
        put32(payload, static_cast<uint32_t>(rle.size()));
        payload.insert(payload.end(), rle.begin(), rle.end());

        // ---- cartridge cover: rounded card = header band + 320x240 screenshot + footer band ----
        const int lh       = font::line_height() > 0 ? font::line_height() : 8;
        const int picX     = kShotX; // fixed screenshot offset (mirrors cartpng.hpp for cropping)
        const int picY     = kShotY;
        const int footerY  = picY + kShotH + kGap2;
        const int footerH  = 3 * lh + 14; // up to three text lines + padding
        const int coverH   = footerY + footerH + kBot;
        const int W        = kW;
        const int needH    = static_cast<int>((payload.size() + W - 1) / W);
        const int H        = needH > coverH ? needH : coverH; // grow past the cover only if data needs it

        std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4);
        auto set = [&](int x, int y, u8 r, u8 g, u8 b)
        {
            if (x < 0 || y < 0 || x >= W || y >= H)
                return;
            uint8_t* d = &px[(static_cast<size_t>(y) * W + x) * 4];
            d[0] = r; d[1] = g; d[2] = b; d[3] = 255;
        };
        auto fill = [&](int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b)
        {
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) set(x, y, r, g, b);
        };
        auto frame = [&](int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b)
        {
            for (int x = x0; x <= x1; ++x) { set(x, y0, r, g, b); set(x, y1, r, g, b); }
            for (int y = y0; y <= y1; ++y) { set(x0, y, r, g, b); set(x1, y, r, g, b); }
        };

        // Cartridge palette (kept independent of the cart's own palette swaps).
        constexpr u8 bodyR = 33, bodyG = 37, bodyB = 58;   // card body
        constexpr u8 bandR = 49, bandG = 55, bandB = 84;   // header/footer bands
        constexpr u8 bzlR = 96, bzlG = 104, bzlB = 148;    // screenshot bezel
        constexpr u8 outR = 12, outG = 14, outB = 30;      // outside the rounded card (blends w/ page)
        constexpr u8 txtR = 240, txtG = 241, txtB = 245;   // title text
        constexpr u8 mutR = 150, mutG = 156, mutB = 180;   // muted text (author / URL / "cartridge")

        fill(0, 0, W - 1, H - 1, bodyR, bodyG, bodyB);              // card body
        fill(0, coverH, W - 1, H - 1, outR, outG, outB);           // data-only rows below the card
        fill(kShotX - 4, 4, W - kShotX + 3, picY - kGap1, bandR, bandG, bandB);          // header band
        fill(kShotX - 4, footerY - 4, W - kShotX + 3, coverH - 6, bandR, bandG, bandB);  // footer band
        frame(picX - 2, picY - 2, picX + kShotW + 1, picY + kShotH + 1, bzlR, bzlG, bzlB); // bezel

        // Screenshot: the captured full-res label, else the sprite sheet in the top-left.
        for (int py = 0; py < kShotH; ++py)
            for (int pxx = 0; pxx < kShotW; ++pxx)
            {
                u8 idx = 0;
                if (label.present)
                    idx = label.px[static_cast<size_t>(py) * CartLabel::kW + pxx];
                else if (pxx < kSheet && py < kSheet)
                    idx = sheet.get(pxx, py);
                const Color32 c = pal.get(idx);
                set(picX + pxx, picY + py, c.r, c.g, c.b);
            }

        // ---- text: a scratch framebuffer renders each string, then we composite it ----
        Framebuffer scratch;
        auto text_w = [](const std::string& s) { return font::text_width(s.c_str()); };
        auto up     = [](std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; };
        // Solid-colour text.
        auto blit = [&](const std::string& s, int x, int y, u8 r, u8 g, u8 b)
        {
            if (s.empty()) return;
            scratch.cls(0);
            font::print(scratch, s.c_str(), 0, 0, 7);
            const int tw = std::min(text_w(s), static_cast<int>(Framebuffer::width()));
            for (int gy = 0; gy < lh; ++gy)
                for (int gx = 0; gx < tw; ++gx)
                    if (scratch.pget(gx, gy) != 0) set(x + gx, y + gy, r, g, b);
        };
        // "LAZY-100" wordmark in per-letter rainbow.
        auto blit_brand = [&](int x, int y)
        {
            static const u8 rainbow[6] = {8, 9, 10, 11, 12, 14};
            const char*     brand = "lazy-100";
            scratch.cls(0);
            int cx = 0, li = 0;
            for (const char* p = brand; *p; ++p, ++li)
            {
                const char cc[2] = {*p, 0};
                font::print(scratch, cc, cx, 0, rainbow[li % 6]);
                cx += font::text_width(cc);
            }
            for (int gy = 0; gy < lh; ++gy)
                for (int gx = 0; gx < cx; ++gx)
                {
                    const u8 idx = scratch.pget(gx, gy);
                    if (idx == 0) continue;
                    const Color32 c = pal.default_at(idx);
                    set(x + gx, y + gy, c.r, c.g, c.b);
                }
            return cx;
        };

        // Header: brand on the left, "fantasy console" on the right.
        const int hy = 4 + (kHeader - lh) / 2;
        blit_brand(kShotX, hy);
        blit(up("fantasy console"), W - kShotX - text_w("FANTASY CONSOLE"), hy, mutR, mutG, mutB);

        // Footer: title, optional "by <author>", then "LAZY-100 CARTRIDGE".
        {
            int        fy   = footerY + 3;
            const std::string t = up(title.empty() ? "untitled" : title);
            blit(t, kShotX, fy, txtR, txtG, txtB);
            fy += lh;
            if (!author.empty()) { blit(up("by " + author), kShotX, fy, mutR, mutG, mutB); fy += lh; }
            blit(up("lazy-100 cartridge"), kShotX, fy, mutR, mutG, mutB);
        }

        // Round the card's corners: fill the four corner cutouts with the outside colour (opaque,
        // so the hidden payload below still round-trips). The card looks rounded against the page.
        constexpr int rad = 14;
        auto cornerCut = [&](int cx, int cy, int sx, int sy)
        {
            for (int dy = 0; dy < rad; ++dy)
                for (int dx = 0; dx < rad; ++dx)
                    if (dx * dx + dy * dy > rad * rad)
                        set(cx + sx * dx, cy + sy * dy, outR, outG, outB);
        };
        cornerCut(rad - 1, rad - 1, -1, -1);              // top-left
        cornerCut(W - rad, rad - 1, 1, -1);               // top-right
        cornerCut(rad - 1, coverH - rad, -1, 1);          // bottom-left
        cornerCut(W - rad, coverH - rad, 1, 1);           // bottom-right

        // Hide the payload in the low 2 bits of each RGBA channel (1 byte per pixel).
        for (size_t i = 0; i < payload.size(); ++i)
        {
            uint8_t*      d = &px[i * 4];
            const uint8_t b = payload[i];
            d[0] = (d[0] & 0xFC) | (b & 0x3);
            d[1] = (d[1] & 0xFC) | ((b >> 2) & 0x3);
            d[2] = (d[2] & 0xFC) | ((b >> 4) & 0x3);
            d[3] = (d[3] & 0xFC) | ((b >> 6) & 0x3);
        }

        if (!stbi_write_png(path.c_str(), W, H, 4, px.data(), W * 4))
        {
            LZ_ERROR("cartpng: failed to write %s", path.c_str());
            return false;
        }
        return true;
    }

    bool load(const std::string& path, std::string& cartText)
    {
        int            w = 0, h = 0, n = 0;
        unsigned char* img = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!img)
        {
            LZ_ERROR("cartpng: cannot read %s", path.c_str());
            return false;
        }
        const size_t total    = static_cast<size_t>(w) * h;
        auto         read_byte = [&](size_t i) -> uint8_t
        {
            const unsigned char* d = &img[i * 4];
            return (d[0] & 3) | ((d[1] & 3) << 2) | ((d[2] & 3) << 4) | ((d[3] & 3) << 6);
        };

        bool ok = false;
        if (total >= 12)
        {
            uint8_t hdr[12];
            for (int i = 0; i < 12; ++i)
                hdr[i] = read_byte(static_cast<size_t>(i));
            if (std::memcmp(hdr, kMagic, 4) == 0)
            {
                const uint32_t rawLen = get32(hdr + 4);
                const uint32_t rleLen = get32(hdr + 8);
                if (12 + static_cast<size_t>(rleLen) <= total)
                {
                    std::vector<uint8_t> rle(rleLen);
                    for (uint32_t i = 0; i < rleLen; ++i)
                        rle[i] = read_byte(12 + i);
                    cartText = rle_decode(rle.data(), rleLen, rawLen);
                    ok       = cartText.size() == rawLen;
                }
            }
        }
        stbi_image_free(img);
        if (!ok)
            LZ_ERROR("cartpng: %s is not a lazy-100 cart PNG", path.c_str());
        return ok;
    }

    bool write_rgba(const std::string& path, int w, int h, const unsigned char* rgba)
    {
        if (!stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4))
        {
            LZ_ERROR("cartpng: cannot write %s", path.c_str());
            return false;
        }
        return true;
    }
} // namespace lazy100::cartpng
