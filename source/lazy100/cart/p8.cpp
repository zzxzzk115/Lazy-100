#include "lazy100/cart/p8.hpp"

#include "lazy100/audio/sound.hpp"
#include "lazy100/common/log.hpp"
#include "lazy100/video/sprites.hpp"
#include "lazy100/world/map.hpp"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------------------------
// .p8 / .p8.png import. ROM layout (both forms normalize into this):
//   0x0000-0x1FFF  gfx: 128x128 4-bit pixels, two per byte, LOW nibble = left pixel
//   0x2000-0x2FFF  map: upper 32 rows of 128 cells (1 byte each)
//                  (rows 32-63 live in the gfx second half, bytes 0x1000-0x1FFF)
//   0x3000-0x30FF  gff: 256 sprite flags
//   0x3100-0x31FF  music: 64 patterns x 4 bytes (channel ids; flags in bits 7 of bytes 0-2)
//   0x3200-0x42FF  sfx: 64 patterns x 68 bytes (32 notes x 2 bytes + mode/speed/loops)
//   0x4300-0x7FFF  code: raw, ":c:" legacy-compressed, or "\0pxa" compressed
// The PNG form hides one ROM byte per pixel in the low 2 bits of A,R,G,B.
// ---------------------------------------------------------------------------------------------

namespace lazy100::p8
{
    namespace
    {
        constexpr int kRomSize  = 0x8000;
        constexpr int kCodeAddr = 0x4300;

        int hexval(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        // ---------------- code decompression (PNG carts) ----------------

        // Legacy ":c:" scheme: lookup-table literals + 2-byte back-references.
        std::string decompress_legacy(const unsigned char* d, int size)
        {
            static const char* lut = "\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_";
            const int   length = d[4] * 256 + d[5];
            std::string out;
            out.reserve(static_cast<size_t>(length));
            for (int i = 8; i < size && static_cast<int>(out.size()) < length; ++i)
            {
                if (d[i] == 0x00)
                    out += static_cast<char>(d[++i]);
                else if (d[i] < 0x3c)
                    out += lut[d[i] - 1];
                else
                {
                    const int offset = (d[i] - 0x3c) * 16 + (d[i + 1] & 0xf);
                    const int len    = (d[i + 1] >> 4) + 2;
                    const int start  = static_cast<int>(out.size()) - offset;
                    if (start < 0)
                        return out; // corrupt stream
                    for (int j = 0; j < len; ++j)
                        out += out[static_cast<size_t>(start + j)];
                    ++i;
                }
            }
            return out;
        }

        // "\0pxa" scheme: LSB-first bitstream, move-to-front literals, LZ back-references.
        std::string decompress_pxa(const unsigned char* d, int size)
        {
            const size_t length     = static_cast<size_t>(d[4]) * 256 + d[5];
            const size_t compressed = std::min<size_t>(static_cast<size_t>(d[6]) * 256 + d[7],
                                                       static_cast<size_t>(size));

            size_t     pos      = 8 * 8; // bit position
            const auto get_bits = [&](size_t count) -> unsigned
            {
                unsigned n = 0;
                for (size_t i = 0; i < count && pos < compressed * 8; ++i, ++pos)
                    n |= static_cast<unsigned>((d[pos >> 3] >> (pos & 7)) & 1) << i;
                return n;
            };

            std::array<unsigned char, 256> mtf {};
            for (int i = 0; i < 256; ++i)
                mtf[static_cast<size_t>(i)] = static_cast<unsigned char>(i);

            std::string out;
            out.reserve(length);
            while (out.size() < length && pos < compressed * 8)
            {
                if (get_bits(1))
                {
                    int nbits = 4;
                    while (get_bits(1))
                        ++nbits;
                    const int n = static_cast<int>(get_bits(static_cast<size_t>(nbits))) +
                                  (1 << nbits) - 16;
                    if (n < 0 || n > 255)
                        break;
                    const unsigned char ch = mtf[static_cast<size_t>(n)];
                    std::rotate(mtf.begin(), mtf.begin() + n, mtf.begin() + n + 1);
                    if (!ch)
                        break;
                    out += static_cast<char>(ch);
                }
                else
                {
                    const int nbits  = get_bits(1) ? (get_bits(1) ? 5 : 10) : 15;
                    const int offset = static_cast<int>(get_bits(static_cast<size_t>(nbits))) + 1;
                    if (nbits == 10 && offset == 1)
                    {
                        // raw byte run, zero-terminated
                        for (unsigned ch = get_bits(8); ch; ch = get_bits(8))
                            out += static_cast<char>(ch);
                    }
                    else
                    {
                        int n = 0, len = 3;
                        do
                            len += (n = static_cast<int>(get_bits(3)));
                        while (n == 7);
                        if (offset > static_cast<int>(out.size()))
                            break; // corrupt stream
                        for (int i = 0; i < len; ++i)
                            out += out[out.size() - static_cast<size_t>(offset)];
                    }
                }
            }
            return out;
        }

        std::string extract_code(const std::vector<unsigned char>& rom)
        {
            const unsigned char* c    = rom.data() + kCodeAddr;
            const int            size = kRomSize - kCodeAddr;
            if (c[0] == '\0' && c[1] == 'p' && c[2] == 'x' && c[3] == 'a')
                return decompress_pxa(c, size);
            if (c[0] == ':' && c[1] == 'c' && c[2] == ':' && c[3] == '\0')
                return decompress_legacy(c, size);
            const void* nul = std::memchr(c, '\0', static_cast<size_t>(size));
            const size_t n  = nul ? static_cast<size_t>(static_cast<const unsigned char*>(nul) - c)
                                  : static_cast<size_t>(size);
            return std::string(reinterpret_cast<const char*>(c), n);
        }

        // ---------------- dialect translation ----------------

        // Mark which bytes of `s` sit inside a string or comment, so the transforms below never
        // touch literal text. Block strings/comments are rare in carts; line-level handling of
        // [[ ]] keeps this simple (a block spanning lines is left untransformed via `inBlock`).
        void mask_line(const std::string& s, std::vector<bool>& lit, bool& inBlock,
                       char& inQuote, int* commentStart = nullptr, bool slashComment = false)
        {
            if (commentStart)
                *commentStart = -1;
            const int n = static_cast<int>(s.size());
            lit.assign(static_cast<size_t>(n), false);
            int start = 0;
            if (inQuote != 0)
            {
                // A quoted string continued past the previous line (a trailing backslash
                // escapes the newline). Mask until it closes - or flag another continuation.
                const char q = inQuote;
                inQuote      = 0;
                bool closed  = false;
                while (start < n)
                {
                    lit[static_cast<size_t>(start)] = true;
                    if (s[static_cast<size_t>(start)] == q)
                    {
                        ++start;
                        closed = true;
                        break;
                    }
                    if (s[static_cast<size_t>(start)] == '\\')
                    {
                        if (start + 1 < n)
                            lit[static_cast<size_t>(start) + 1] = true;
                        start += 2;
                        continue;
                    }
                    ++start;
                }
                if (!closed && n > 0 && s[static_cast<size_t>(n) - 1] == '\\')
                    inQuote = q;
            }
            for (int i = start; i < n;)
            {
                if (inBlock)
                {
                    lit[static_cast<size_t>(i)] = true;
                    if (s.compare(static_cast<size_t>(i), 2, "]]") == 0)
                    {
                        if (i + 1 < n)
                            lit[static_cast<size_t>(i) + 1] = true;
                        inBlock = false;
                        i += 2;
                        continue;
                    }
                    ++i;
                    continue;
                }
                const char c = s[static_cast<size_t>(i)];
                if (slashComment && c == '/' && i + 1 < n && s[static_cast<size_t>(i) + 1] == '/')
                {
                    // the dialect's C-style line comment (its integer division is backslash)
                    if (commentStart)
                        *commentStart = i;
                    for (int j = i; j < n; ++j)
                        lit[static_cast<size_t>(j)] = true;
                    break;
                }
                if (c == '-' && i + 1 < n && s[static_cast<size_t>(i) + 1] == '-')
                {
                    if (s.compare(static_cast<size_t>(i), 4, "--[[") == 0)
                    {
                        inBlock = true;
                        for (int j = i; j < i + 4 && j < n; ++j)
                            lit[static_cast<size_t>(j)] = true;
                        i += 4;
                        continue;
                    }
                    if (commentStart)
                        *commentStart = i;
                    for (int j = i; j < n; ++j)
                        lit[static_cast<size_t>(j)] = true;
                    break;
                }
                if (c == '[' && i + 1 < n && s[static_cast<size_t>(i) + 1] == '[')
                {
                    inBlock                     = true;
                    lit[static_cast<size_t>(i)] = true;
                    lit[static_cast<size_t>(i) + 1] = true;
                    i += 2;
                    continue;
                }
                if (c == '"' || c == '\'')
                {
                    lit[static_cast<size_t>(i)] = true;
                    int j                       = i + 1;
                    while (j < n && s[static_cast<size_t>(j)] != c)
                    {
                        lit[static_cast<size_t>(j)] = true;
                        j += (s[static_cast<size_t>(j)] == '\\' && j + 1 < n) ? 2 : 1;
                        if (j - 1 < n && j - 1 > i)
                            lit[static_cast<size_t>(j) - 1] = true;
                    }
                    if (j < n)
                        lit[static_cast<size_t>(j)] = true;
                    else if (n > 0 && s[static_cast<size_t>(n) - 1] == '\\')
                        inQuote = c; // escaped newline: the string continues on the next line
                    i = j + 1;
                    continue;
                }
                ++i;
            }
        }

        bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        // One line of dialect -> standard Lua. `lit` marks string/comment bytes to skip.
        std::string translate_line(std::string s, const std::vector<bool>& litIn, char qStart)
        {
            std::vector<bool> lit = litIn;
            const auto        resync = [&]
            {
                bool dummy = false;
                char q     = qStart;
                mask_line(s, lit, dummy, q);
            };

            // The dialect's lexer ends a number at the first non-numeric character, so
            // `val=5end` is two tokens; Lua would read a malformed exponent. Split a keyword
            // glued to a number with a space.
            for (const char* w : {"end", "then", "do", "else", "elseif", "and", "or", "not",
                                  "until", "in"})
            {
                const size_t wl = std::strlen(w);
                for (size_t i = 1; i + wl <= s.size(); ++i)
                {
                    if (lit[i] || s.compare(i, wl, w) != 0)
                        continue;
                    if (!std::isdigit(static_cast<unsigned char>(s[i - 1])))
                        continue;
                    if (i + wl < s.size() && ident_char(s[i + wl]))
                        continue;
                    // the digit must belong to a number, not an identifier like `x2end`
                    size_t t = i;
                    while (t > 0 && (ident_char(s[t - 1]) || s[t - 1] == '.'))
                        --t;
                    if (!std::isdigit(static_cast<unsigned char>(s[t])))
                        continue;
                    s.insert(i, " ");
                    resync();
                    ++i;
                }
            }

            // '#include' has no meaning here
            {
                size_t i = s.find_first_not_of(" \t");
                if (i != std::string::npos && s.compare(i, 8, "#include") == 0)
                    return "-- " + s;
            }

            // button glyphs used as constants outside strings -> the numeric button ids
            // (left/right/up/down/o/x). Inside strings they are left alone (display only).
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (lit[i])
                    continue;
                int btn = -1;
                switch (static_cast<unsigned char>(s[i]))
                {
                    case 0x8B: btn = 0; break; // left arrow
                    case 0x91: btn = 1; break; // right arrow
                    case 0x94: btn = 2; break; // up arrow
                    case 0x83: btn = 3; break; // down arrow
                    case 0x8E: btn = 4; break; // o button
                    case 0x97: btn = 5; break; // x button
                    default: break;
                }
                if (btn >= 0)
                {
                    s.replace(i, 1, std::to_string(btn));
                    resync();
                }
                else if (static_cast<unsigned char>(s[i]) >= 0x80)
                {
                    // Any other glyph acts as an identifier character there (p8 predefines
                    // one-glyph constants like fill patterns). Rewrite it as a plain safe
                    // identifier - undefined ones read as nil, which our stubs accept.
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "__g%02x", static_cast<unsigned char>(s[i]));
                    s.replace(i, 1, buf);
                    resync();
                }
            }

            // '?expr[,x,y,c]' -> print(expr[,x,y,c]) - wherever a statement or expression can
            // begin: line start, after '=' (assignment/return-value forms), after ')' (the
            // one-line if body), or after return/do/then/else. The argument list runs to the
            // end of the line but stops before a comment or a top-level closing keyword.
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (lit[i] || s[i] != '?')
                    continue;
                size_t pv = i;
                while (pv > 0 && (s[pv - 1] == ' ' || s[pv - 1] == '\t'))
                    --pv;
                bool ok = (pv == 0);
                if (!ok)
                {
                    const char pc = s[pv - 1];
                    if (pc == '=' || pc == ')')
                        ok = true;
                    else if (ident_char(pc))
                    {
                        size_t ws = pv;
                        while (ws > 0 && ident_char(s[ws - 1]))
                            --ws;
                        const std::string w = s.substr(ws, pv - ws);
                        ok = (w == "return" || w == "do" || w == "then" || w == "else");
                    }
                }
                if (!ok)
                    continue;
                // Body end: comment, or a top-level end/else/elseif closing an outer block.
                int csAt = -1;
                {
                    std::vector<bool> tmp;
                    bool              blk = false;
                    char              q   = qStart;
                    mask_line(s, tmp, blk, q, &csAt);
                }
                size_t stmtEnd = (csAt >= 0 && static_cast<size_t>(csAt) > i)
                                     ? static_cast<size_t>(csAt)
                                     : s.size();
                int d2 = 0;
                for (size_t k = i + 1; k < stmtEnd; ++k)
                {
                    if (lit[k])
                        continue;
                    const char c = s[k];
                    if (c == '(' || c == '[' || c == '{')
                        ++d2;
                    else if (c == ')' || c == ']' || c == '}')
                        --d2;
                    else if (d2 == 0 && ident_char(c) && (k == 0 || !ident_char(s[k - 1])))
                    {
                        for (const char* w : {"end", "else", "elseif"})
                        {
                            const size_t wl = std::strlen(w);
                            if (s.compare(k, wl, w) == 0 &&
                                (k + wl >= s.size() || !ident_char(s[k + wl])))
                            {
                                stmtEnd = k;
                                break;
                            }
                        }
                        if (stmtEnd == k)
                            break;
                    }
                }
                s = s.substr(0, i) + "print(" + s.substr(i + 1, stmtEnd - i - 1) + ")" +
                    s.substr(stmtEnd);
                resync();
            }

            // Escape sequences Lua doesn't know (the p8 print control codes like "\^w", "\#x"):
            // double the backslash so the string stays valid (the code is then literal text).
            for (size_t i = 0; i + 1 < s.size(); ++i)
            {
                if (s[i] != '\\' || !lit[i])
                    continue;
                static const char* kValid = "abfnrtvz\\\"'x0123456789u";
                if (std::strchr(kValid, s[i + 1]) == nullptr)
                {
                    s.insert(i, 1, '\\');
                    resync();
                }
                ++i; // skip the (now valid) escaped character
            }

            // '!='  ->  '~='
            for (size_t i = 0; i + 1 < s.size(); ++i)
                if (!lit[i] && s[i] == '!' && s[i + 1] == '=')
                    s[i] = '~';

            // binary literals 0b1010 -> decimal
            for (size_t i = 0; i + 1 < s.size();)
            {
                if (!lit[i] && s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B') &&
                    (i == 0 || !ident_char(s[i - 1])))
                {
                    size_t   j = i + 2;
                    long long v = 0;
                    while (j < s.size() && (s[j] == '0' || s[j] == '1'))
                        v = v * 2 + (s[j++] - '0');
                    if (j > i + 2)
                    {
                        s = s.substr(0, i) + std::to_string(v) + s.substr(j);
                        resync();
                        continue;
                    }
                }
                ++i;
            }

            // xor: p8 writes `a ^^ b`; Lua's operator is binary `~`. Compound `^^=` is left
            // for the block below (turning it into `~=` here would read as inequality).
            for (size_t i = 0; i + 1 < s.size(); ++i)
                if (!lit[i] && s[i] == '^' && s[i + 1] == '^' &&
                    (i + 2 >= s.size() || s[i + 2] != '='))
                {
                    s.replace(i, 2, "~");
                    resync();
                }

            // compound assignment:  lhs op= rhs  ->  lhs = lhs op (rhs)
            for (size_t i = 1; i < s.size(); ++i)
            {
                if (lit[i] || s[i] != '=')
                    continue;
                if (i + 1 < s.size() && s[i + 1] == '=')
                {
                    ++i;
                    continue;
                }
                const char prev = s[i - 1];
                std::string op;
                size_t      opStart = i - 1;
                if (prev == '^' && i >= 2 && s[i - 2] == '^')
                {
                    op      = "^^"; // p8 xor-assign
                    opStart = i - 2;
                }
                else if ((prev == '<' || prev == '>') && i >= 2 && s[i - 2] == prev)
                {
                    op      = std::string(2, prev); // <<= / >>= (a single <=/>= is a comparison)
                    opStart = i - 2;
                }
                else if (prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%' ||
                         prev == '^' || prev == '\\' || prev == '|' || prev == '&')
                    op = std::string(1, prev);
                else if (prev == '.' && i >= 2 && s[i - 2] == '.')
                {
                    op      = "..";
                    opStart = i - 2;
                }
                else
                    continue;
                if (lit[opStart])
                    continue;
                // lvalue: skip blanks before the operator, then scan identifier / field / index
                // chars backwards (brackets balanced)
                int lhsEnd = static_cast<int>(opStart);
                while (lhsEnd > 0 && (s[static_cast<size_t>(lhsEnd) - 1] == ' ' ||
                                      s[static_cast<size_t>(lhsEnd) - 1] == '\t'))
                    --lhsEnd;
                int  a     = lhsEnd;
                int  depth = 0;
                while (a > 0)
                {
                    const char c = s[static_cast<size_t>(a) - 1];
                    if (c == ']')
                        ++depth;
                    else if (c == '[')
                    {
                        if (--depth < 0)
                            break;
                    }
                    else if (depth == 0 && !(ident_char(c) || c == '.'))
                        break;
                    --a;
                }
                std::string lhs =
                    s.substr(static_cast<size_t>(a), static_cast<size_t>(lhsEnd) - static_cast<size_t>(a));
                // trim
                while (!lhs.empty() && lhs.back() == ' ')
                    lhs.pop_back();
                while (!lhs.empty() && lhs.front() == ' ')
                    lhs.erase(lhs.begin());
                if (lhs.empty())
                    continue;
                // rhs runs to end of line, but stops before a trailing comment, before a
                // top-level `end/else/elseif` keyword, and before the NEXT compound
                // assignment: p8 allows several space-separated statements on one line
                // ("x/=d y/=d z/=d").
                size_t rhsStart = i + 1;
                size_t rhsEnd   = s.size();
                {
                    int csAt = -1;
                    std::vector<bool> tmp;
                    bool              blk = false;
                    char              q   = qStart;
                    mask_line(s, tmp, blk, q, &csAt);
                    if (csAt >= 0 && static_cast<size_t>(csAt) > rhsStart)
                        rhsEnd = static_cast<size_t>(csAt);
                }
                const size_t rhsCap = rhsEnd;
                int d2 = 0;
                for (size_t k = rhsStart; k < rhsCap; ++k)
                {
                    if (lit[k])
                        continue;
                    const char c = s[k];
                    if (d2 == 0 && c == ';') // explicit statement separator ends the rhs
                    {
                        rhsEnd = k;
                        break;
                    }
                    if (c == '(' || c == '[' || c == '{')
                        ++d2;
                    else if (c == ')' || c == ']' || c == '}')
                        --d2;
                    else if (d2 == 0 && ident_char(c) && (k == 0 || !ident_char(s[k - 1])))
                    {
                        static const char* kw[] = {"end", "else", "elseif"};
                        for (const char* w : kw)
                        {
                            const size_t wl = std::strlen(w);
                            if (s.compare(k, wl, w) == 0 &&
                                (k + wl >= s.size() || !ident_char(s[k + wl])))
                            {
                                rhsEnd = k;
                                break;
                            }
                        }
                        if (rhsEnd != rhsCap)
                            break;
                        // `ident op=` ahead? Then this identifier starts a new statement.
                        size_t e = k;
                        while (e < s.size() && (ident_char(s[e]) || s[e] == '.'))
                            ++e;
                        size_t o = e;
                        while (o < s.size() && (s[o] == ' ' || s[o] == '\t'))
                            ++o;
                        bool compoundAhead = false;
                        if (e > k && o + 1 < s.size())
                        {
                            const char c1 = s[o], c2 = s[o + 1];
                            if (c1 == c2 && (c1 == '<' || c1 == '>' || c1 == '^') &&
                                o + 2 < s.size() && s[o + 2] == '=')
                                compoundAhead = true; // <<= / >>= / ^^=
                            else if ((c1 == '+' || c1 == '-' || c1 == '*' || c1 == '/' ||
                                      c1 == '%' || c1 == '^' || c1 == '\\' || c1 == '|' ||
                                      c1 == '&') &&
                                     c2 == '=')
                                compoundAhead = true;
                        }
                        if (compoundAhead)
                        {
                            rhsEnd = k;
                            break;
                        }
                        // General new-statement heuristic: an identifier right after a token
                        // that finishes an expression (Lua has no juxtaposition) starts a new
                        // statement - `p_trim-=1/64 sfx(9,1)` - unless it's a keyword that
                        // continues the expression.
                        {
                            size_t pv = k;
                            while (pv > rhsStart && (s[pv - 1] == ' ' || s[pv - 1] == '\t'))
                                --pv;
                            const char pc = pv > rhsStart ? s[pv - 1] : '\0';
                            const bool exprEnd = ident_char(pc) || pc == ')' || pc == ']' ||
                                                 pc == '}' || pc == '"' || pc == '\'';
                            if (exprEnd)
                            {
                                const std::string w = s.substr(k, e - k);
                                static const char* cont[] = {"and", "or", "not"};
                                bool continues = false;
                                for (const char* c2 : cont)
                                    if (w == c2)
                                        continues = true;
                                // ..but `not` after an expression is also a new statement's
                                // operand; only and/or truly continue. Keep `not` safe anyway.
                                if (!continues)
                                {
                                    rhsEnd = k;
                                    break;
                                }
                            }
                        }
                    }
                }
                std::string rhs = s.substr(rhsStart, rhsEnd - rhsStart);
                const std::string luaOp = (op == "\\") ? "//" : (op == "^^") ? "~" : op;
                s = s.substr(0, static_cast<size_t>(a)) + lhs + " = " + lhs + " " + luaOp + " (" +
                    rhs + ") " + s.substr(rhsEnd);
                resync();
                i = static_cast<size_t>(a); // keep scanning: more `op=` may follow on this line
            }

            // integer division a \ b -> a // b (any '\\' still left outside literals)
            for (size_t i = 0; i < s.size(); ++i)
                if (!lit[i] && s[i] == '\\')
                {
                    s.replace(i, 1, "//");
                    resync();
                }

            // memory-read operators: @a -> peek(a), %a -> peek2(a), $a -> peek4(a).
            // The operand is the following primary expression: an identifier/number chain
            // (dots included) optionally followed by balanced ()/[] - or a parenthesized
            // expression. '%' doubles as modulo: it only reads memory in unary position
            // (nothing value-like right before it).
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (lit[i])
                    continue;
                const char op = s[i];
                if (op != '@' && op != '%' && op != '$')
                    continue;
                if (op == '%')
                {
                    size_t p = i;
                    while (p > 0 && (s[p - 1] == ' ' || s[p - 1] == '\t'))
                        --p;
                    if (p > 0 && (ident_char(s[p - 1]) || s[p - 1] == ')' || s[p - 1] == ']'))
                        continue; // binary modulo
                }
                // Parse the operand.
                size_t a = i + 1;
                while (a < s.size() && (s[a] == ' ' || s[a] == '\t'))
                    ++a;
                size_t b = a;
                if (b < s.size() && s[b] == '(')
                {
                    int depth = 0;
                    while (b < s.size())
                    {
                        if (!lit[b] && s[b] == '(')
                            ++depth;
                        else if (!lit[b] && s[b] == ')' && --depth == 0)
                        {
                            ++b;
                            break;
                        }
                        ++b;
                    }
                }
                else
                {
                    while (b < s.size() && (ident_char(s[b]) || s[b] == '.'))
                        ++b;
                    while (b < s.size() && (s[b] == '(' || s[b] == '[')) // call / index chain
                    {
                        const char open = s[b], close = (open == '(') ? ')' : ']';
                        int        depth = 0;
                        while (b < s.size())
                        {
                            if (!lit[b] && s[b] == open)
                                ++depth;
                            else if (!lit[b] && s[b] == close && --depth == 0)
                            {
                                ++b;
                                break;
                            }
                            ++b;
                        }
                    }
                }
                if (b == a) // no operand (e.g. a stray '%'): leave it alone
                    continue;
                const char* fn = (op == '@') ? "peek(" : (op == '%') ? "peek2(" : "peek4(";
                s = s.substr(0, i) + fn + s.substr(a, b - a) + ")" + s.substr(b);
                resync();
            }

            // Bitwise operators work on 16.16 fixed-point values in the source dialect
            // (fractional masks like `x & 0x3fff.ffff` are idiomatic); Lua's native operators
            // demand integers. Rewrite infix bitwise expressions into calls to the prelude's
            // fixed-point helpers. Operands are grabbed as primary expressions - source code
            // parenthesizes mixed arithmetic around bitwise ops by convention.
            {
                // Forward primary: identifier/number chain (dots included) with call/index
                // suffixes, or a parenthesized expression.
                const auto grab_fwd = [&](size_t from, size_t& outA, size_t& outB) -> bool
                {
                    size_t a2 = from;
                    while (a2 < s.size() && (s[a2] == ' ' || s[a2] == '\t'))
                        ++a2;
                    size_t b2 = a2;
                    if (b2 < s.size() && s[b2] == '(')
                    {
                        int depth = 0;
                        while (b2 < s.size())
                        {
                            if (!lit[b2] && s[b2] == '(')
                                ++depth;
                            else if (!lit[b2] && s[b2] == ')' && --depth == 0)
                            {
                                ++b2;
                                break;
                            }
                            ++b2;
                        }
                    }
                    else
                    {
                        while (b2 < s.size() && (ident_char(s[b2]) || s[b2] == '.'))
                            ++b2;
                        while (b2 < s.size() && (s[b2] == '(' || s[b2] == '['))
                        {
                            const char open = s[b2], closec = (open == '(') ? ')' : ']';
                            int        depth = 0;
                            while (b2 < s.size())
                            {
                                if (!lit[b2] && s[b2] == open)
                                    ++depth;
                                else if (!lit[b2] && s[b2] == closec && --depth == 0)
                                {
                                    ++b2;
                                    break;
                                }
                                ++b2;
                            }
                        }
                    }
                    outA = a2;
                    outB = b2;
                    return b2 > a2;
                };
                // Backward primary: mirror image, walking left over )-chains and ident chars.
                const auto grab_back = [&](size_t upto, size_t& outA, size_t& outB) -> bool
                {
                    size_t e2 = upto;
                    while (e2 > 0 && (s[e2 - 1] == ' ' || s[e2 - 1] == '\t'))
                        --e2;
                    size_t a2 = e2;
                    while (a2 > 0)
                    {
                        const char pc = s[a2 - 1];
                        if (pc == ')' || pc == ']')
                        {
                            const char closec = pc, open = (pc == ')') ? '(' : '[';
                            int        depth  = 0;
                            while (a2 > 0)
                            {
                                const char cc = s[a2 - 1];
                                if (!lit[a2 - 1] && cc == closec)
                                    ++depth;
                                else if (!lit[a2 - 1] && cc == open && --depth == 0)
                                {
                                    --a2;
                                    break;
                                }
                                --a2;
                            }
                        }
                        else if (ident_char(pc) || pc == '.')
                            --a2;
                        else
                            break;
                    }
                    outA = a2;
                    outB = e2;
                    return e2 > a2;
                };

                // Conversion happens in PRECEDENCE TIERS (unary ~, then shifts, then &, then
                // xor, then |), so a mixed expression like `read()|read()<<8` folds the shift
                // before the or - matching the dialect's C-style precedence. Within a tier the
                // rescan keeps left associativity.
                for (int tier = 0; tier < 5; ++tier)
                {
                    for (size_t i = 0; i < s.size(); ++i)
                    {
                        if (lit[i])
                            continue;
                        const char  c     = s[i];
                        const char* fn    = nullptr;
                        size_t      opLen = 1;
                        bool        unary = false;
                        switch (tier)
                        {
                            case 0: // unary bnot
                                if (c == '~' && (i + 1 >= s.size() || s[i + 1] != '='))
                                {
                                    size_t pv = i;
                                    while (pv > 0 && (s[pv - 1] == ' ' || s[pv - 1] == '\t'))
                                        --pv;
                                    const char pc = pv > 0 ? s[pv - 1] : '\0';
                                    if (!(ident_char(pc) || pc == ')' || pc == ']'))
                                    {
                                        fn    = "bnot";
                                        unary = true;
                                    }
                                }
                                break;
                            case 1: // shifts
                                if (c == '>' && s.compare(i, 3, ">>>") == 0)
                                {
                                    fn    = "lshr";
                                    opLen = 3;
                                }
                                else if (c == '>' && s.compare(i, 2, ">>") == 0 &&
                                         (i + 2 >= s.size() || s[i + 2] != '='))
                                {
                                    fn    = "shr";
                                    opLen = 2;
                                }
                                else if (c == '<' && s.compare(i, 2, "<<") == 0 &&
                                         (i + 2 >= s.size() || s[i + 2] != '='))
                                {
                                    fn    = "shl";
                                    opLen = 2;
                                }
                                break;
                            case 2:
                                if (c == '&' && (i + 1 >= s.size() || s[i + 1] != '='))
                                    fn = "band";
                                break;
                            case 3: // binary xor (from the ^^ rewrite)
                                if (c == '~' && (i + 1 >= s.size() || s[i + 1] != '='))
                                {
                                    size_t pv = i;
                                    while (pv > 0 && (s[pv - 1] == ' ' || s[pv - 1] == '\t'))
                                        --pv;
                                    const char pc = pv > 0 ? s[pv - 1] : '\0';
                                    if (ident_char(pc) || pc == ')' || pc == ']')
                                        fn = "bxor";
                                }
                                break;
                            case 4:
                                if (c == '|' && (i + 1 >= s.size() || s[i + 1] != '='))
                                    fn = "bor";
                                break;
                        }
                        if (!fn)
                            continue;
                        size_t ra = 0, rb = 0;
                        if (!grab_fwd(i + opLen, ra, rb))
                            continue;
                        if (unary)
                        {
                            s = s.substr(0, i) + std::string(fn) + "(" + s.substr(ra, rb - ra) +
                                ")" + s.substr(rb);
                            resync();
                            continue;
                        }
                        size_t la = 0, lb = 0;
                        if (!grab_back(i, la, lb))
                            continue;
                        s = s.substr(0, la) + std::string(fn) + "(" + s.substr(la, lb - la) + "," +
                            s.substr(ra, rb - ra) + ")" + s.substr(rb);
                        resync();
                        i = la; // rescan from the call: chained ops fold left-to-right
                    }
                }
            }

            // `_ENV = t` idiom: the dialect keeps built-ins reachable after an environment
            // swap; stock Lua would lose them. Route the table through __envwrap, which
            // installs a global-fallback metatable.
            for (size_t i = 0; i + 4 <= s.size(); ++i)
            {
                if (lit[i] || s.compare(i, 4, "_ENV") != 0)
                    continue;
                if ((i > 0 && ident_char(s[i - 1])) || (i + 4 < s.size() && ident_char(s[i + 4])))
                    continue;
                size_t e = i + 4;
                while (e < s.size() && (s[e] == ' ' || s[e] == '	'))
                    ++e;
                if (e >= s.size() || s[e] != '=' || (e + 1 < s.size() && s[e + 1] == '='))
                    continue;
                size_t a = e + 1;
                while (a < s.size() && (s[a] == ' ' || s[a] == '	'))
                    ++a;
                size_t b = a;
                if (b < s.size() && s[b] == '(')
                {
                    int depth = 0;
                    while (b < s.size())
                    {
                        if (!lit[b] && s[b] == '(')
                            ++depth;
                        else if (!lit[b] && s[b] == ')' && --depth == 0)
                        {
                            ++b;
                            break;
                        }
                        ++b;
                    }
                }
                else
                {
                    while (b < s.size() && (ident_char(s[b]) || s[b] == '.'))
                        ++b;
                    while (b < s.size() && (s[b] == '(' || s[b] == '['))
                    {
                        const char open = s[b], closec = (open == '(') ? ')' : ']';
                        int        depth = 0;
                        while (b < s.size())
                        {
                            if (!lit[b] && s[b] == open)
                                ++depth;
                            else if (!lit[b] && s[b] == closec && --depth == 0)
                            {
                                ++b;
                                break;
                            }
                            ++b;
                        }
                    }
                }
                if (b == a)
                    continue;
                s = s.substr(0, a) + "__envwrap(" + s.substr(a, b - a) + ")" + s.substr(b);
                resync();
                i = b + 10;
            }

            // one-line 'if (cond) stmt' / 'while (cond) stmt' shorthand - anywhere on the line
            // (it also appears mid-line, e.g. inside a one-line function definition). The body
            // runs to the end of the line but stops before a trailing comment and before any
            // TOP-LEVEL end/else/elseif, which belongs to an enclosing block, not the body.
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (lit[i] || !(s[i] == 'i' || s[i] == 'w'))
                    continue;
                if (i > 0 && ident_char(s[i - 1]))
                    continue;
                const bool isIf    = s.compare(i, 2, "if") == 0 && i + 2 < s.size() &&
                                  !ident_char(s[i + 2]);
                const bool isWhile = s.compare(i, 5, "while") == 0 && i + 5 < s.size() &&
                                     !ident_char(s[i + 5]);
                if (!isIf && !isWhile)
                    continue;
                const size_t p = s.find_first_not_of(" \t", i + (isIf ? 2 : 5));
                if (p == std::string::npos || s[p] != '(' || lit[p])
                    continue;
                int    depth = 0;
                size_t close = std::string::npos;
                for (size_t k = p; k < s.size(); ++k)
                {
                    if (lit[k])
                        continue;
                    if (s[k] == '(')
                        ++depth;
                    else if (s[k] == ')' && --depth == 0)
                    {
                        close = k;
                        break;
                    }
                }
                if (close == std::string::npos)
                    continue;
                // Statement end: the earliest of a trailing comment or a top-level
                // end/else/elseif keyword (both belong outside the shorthand body).
                int csAt = -1;
                {
                    std::vector<bool> tmp;
                    bool              blk = false;
                    char              q   = qStart;
                    mask_line(s, tmp, blk, q, &csAt);
                }
                size_t stmtEnd = (csAt >= 0 && static_cast<size_t>(csAt) > close)
                                     ? static_cast<size_t>(csAt)
                                     : s.size();
                int d2 = 0;
                for (size_t k = close + 1; k < stmtEnd; ++k)
                {
                    if (lit[k])
                        continue;
                    const char c = s[k];
                    if (c == '(' || c == '[' || c == '{')
                        ++d2;
                    else if (c == ')' || c == ']' || c == '}')
                        --d2;
                    else if (d2 == 0 && ident_char(c) && (k == 0 || !ident_char(s[k - 1])))
                    {
                        for (const char* w : {"end", "else", "elseif"})
                        {
                            const size_t wl = std::strlen(w);
                            if (s.compare(k, wl, w) == 0 &&
                                (k + wl >= s.size() || !ident_char(s[k + wl])))
                            {
                                stmtEnd = k;
                                break;
                            }
                        }
                        if (stmtEnd == k)
                            break;
                    }
                }
                const std::string rest = s.substr(close + 1, stmtEnd - close - 1);
                const std::string tail = s.substr(stmtEnd);
                // shorthand only when a statement follows with no `then`/`do` (and the
                // condition doesn't continue with and/or past the parens)
                bool plain = rest.find_first_not_of(" \t") != std::string::npos;
                for (const char* w : {"then", "do", "and", "or"})
                {
                    const size_t f = rest.find(w);
                    if (f != std::string::npos && (f == 0 || !ident_char(rest[f - 1])) &&
                        (f + std::strlen(w) >= rest.size() || !ident_char(rest[f + std::strlen(w)])))
                    {
                        if (std::strcmp(w, "and") == 0 || std::strcmp(w, "or") == 0)
                        {
                            const size_t nb = rest.find_first_not_of(" \t");
                            if (nb == f) // condition continues past the parens
                                plain = false;
                        }
                        else
                            plain = false;
                    }
                }
                if (plain)
                {
                    s = s.substr(0, close + 1) + (isIf ? " then " : " do ") + rest + " end " + tail;
                    resync();
                    i = close; // keep scanning: the body may hold another shorthand
                }
            }

            return s;
        }

        std::string translate(const std::string& code)
        {
            std::string out;
            out.reserve(code.size() + code.size() / 8);
            std::istringstream in(code);
            std::string        line;
            bool               inBlock = false;
            char               inQuote = 0;
            std::vector<bool>  lit;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const bool wasBlock = inBlock;
                const char qStart   = inQuote;
                int        cs       = -1;
                mask_line(line, lit, inBlock, inQuote, &cs, true);
                if (cs >= 0 && line[static_cast<size_t>(cs)] == '/')
                {
                    // '//' comments become '--' so every later re-mask agrees (our division
                    // output also uses '//', so the slash form must vanish up front)
                    line[static_cast<size_t>(cs)]     = '-';
                    line[static_cast<size_t>(cs) + 1] = '-';
                }
                out += wasBlock ? line : translate_line(line, lit, qStart);
                out += '\n';
            }
            return out;
        }

        // ---------------- runtime shim ----------------

        // Runs before the cart: a 128x128 viewport centered on our 320x240 screen, 8px sprite
        // and map semantics, flags from the imported gff table, and legacy helpers.
        constexpr const char* kPrelude = R"lua(
-- p8 ext compatibility layer (generated; edit freely)
__p8 = { ox = 96, oy = 56 }
do
  local ox, oy = __p8.ox, __p8.oy
  local _camera, _clip, _rectfill, _sspr, _rect, _print = camera, clip, rectfill, sspr, rect, print
  camera = function(x, y) _camera((x or 0) - ox, (y or 0) - oy) end
  clip = function(x, y, w, h)
    if x then _clip(mid(0, x, 128) + ox, mid(0, y, 128) + oy, w, h)
    else _clip(ox, oy, 128, 128) end
  end
  cls = function(c) _rectfill(0, 0, 127, 127, c or 0) end
  spr = function(n, x, y, w, h, fx, fy)
    n = flr(n or 0)
    w = w or 1 h = h or 1
    _sspr((n % 16) * 8, flr(n / 16) * 8, w * 8, h * 8, x or 0, y or 0, w * 8, h * 8, fx, fy)
  end
  local _fget_t = __p8_gff or {}
  fget = function(n, b)
    local f = _fget_t[flr(n or 0) + 1] or 0
    if b then return (f >> flr(b)) & 1 == 1 end
    return f
  end
  fset = function(n, a, b)
    n = flr(n or 0) + 1
    local f = _fget_t[n] or 0
    if b == nil then _fget_t[n] = flr(a or 0)
    elseif b then _fget_t[n] = f | (1 << flr(a))
    else _fget_t[n] = f & ~(1 << flr(a)) end
  end
  map = function(cx, cy, sx, sy, cw, ch, layers)
    cx = flr(cx or 0) cy = flr(cy or 0)
    sx = sx or 0 sy = sy or 0
    cw = flr(cw or 128) ch = flr(ch or 64)
    for j = 0, ch - 1 do
      for i = 0, cw - 1 do
        local n = mget(cx + i, cy + j)
        if n ~= 0 and n ~= 255 then
          if not layers or (fget(n) & layers) == layers then
            spr(n, sx + i * 8, sy + j * 8)
          end
        end
      end
    end
  end
  mapdraw = map
  -- pal: translate the dialect's "secret" palette (128-143) to our extended block (16-31),
  -- and accept the table form pal({[c]=v, ...}, mode).
  do
    local _pal = pal
    pal = function(a, b, m)
      if a == nil then
        _pal()
        return
      end
      if type(a) == "table" then
        for k, v in pairs(a) do
          pal(k, v, b)
        end
        return
      end
      if b ~= nil and b >= 128 and m ~= 2 then b = (b % 16) + 16 end
      _pal(a, b, m)
    end
  end
  -- tline: textured line - walk the screen segment one pixel per dominant-axis step,
  -- sampling the map (8px tiles) at (mx,my), which advances by (mdx,mdy) per pixel.
  tline = function(x0, y0, x1, y1, mx, my, mdx, mdy)
    mdx = mdx or 0.125
    mdy = mdy or 0
    local n = max(abs(x1 - x0), abs(y1 - y0))
    local ddx, ddy = 0, 0
    if n > 0 then ddx = (x1 - x0) / n ddy = (y1 - y0) / n end
    local x, y = x0, y0
    for i = 0, n do
      local t = mget(flr(mx), flr(my))
      if t and t ~= 0 and t ~= 255 then
        local c = sget((t % 16) * 8 + flr(mx * 8) % 8, flr(t / 16) * 8 + flr(my * 8) % 8)
        if c ~= 0 then pset(x, y, c) end -- color 0 stays transparent, like sprites
      end
      x = x + ddx
      y = y + ddy
      mx = mx + mdx
      my = my + mdy
    end
  end
  function __p8.setup()
    -- wipe anything drawn before _init (top-level cart code runs unclipped otherwise),
    -- then rebuild the viewport and its frame
    _camera(0, 0)
    _clip()
    _rectfill(0, 0, 319, 239, 0)
    camera()
    clip()
    __p8.frame()
  end
  function __p8.frame()
    -- console-shell chrome drawn in screen space every frame: panel fill (covers anything a
    -- cart leaks outside the viewport), double bezel, color-stripe badge, ext tag
    local fps = __fillp_save() -- the cart's dither pattern must not bleed into the chrome
    __fillp_restore(0)
    _clip()
    _camera(0, 0)
    _rectfill(0, 0, 319, oy - 3, 1)                 -- top panel
    _rectfill(0, oy + 130, 319, 239, 1)             -- bottom panel
    _rectfill(0, oy - 2, ox - 4, oy + 129, 1)       -- left panel
    _rectfill(ox + 131, oy - 2, 319, oy + 129, 1)   -- right panel
    _rect(ox - 3, oy - 3, ox + 130, oy + 130, 13)   -- outer bezel
    _rect(ox - 2, oy - 2, ox + 129, oy + 129, 5)
    _rect(ox - 1, oy - 1, ox + 128, oy + 128, 0)
    local bx, by = ox, oy + 134                     -- cartridge badge: stripes + tag
    _rectfill(bx, by, bx + 5, by + 3, 8)
    _rectfill(bx + 6, by, bx + 11, by + 3, 9)
    _rectfill(bx + 12, by, bx + 17, by + 3, 10)
    _rectfill(bx + 18, by, bx + 23, by + 3, 11)
    _print("p8 ext cartridge", ox + 30, oy + 131, 6)
    _print("LAZY-100", ox + 90, oy - 12, 13)
    __fillp_restore(fps)
    camera()
    clip()
  end
  -- p8 bitmap-font print: cursor state, the print(text, color) two-argument form,
  -- and \^w/\^t wide-tall control codes (rendered by the native __p8_print)
  do
    local _pp = __p8_print
    local px_cur, py_cur = 0, 0
    local _cls0 = cls
    cls = function(c) -- p8 cls also homes the print cursor
      px_cur, py_cur = 0, 0
      return _cls0(c)
    end
    __p8_cursor = function(x, y)
      px_cur, py_cur = x or 0, y or 0
    end
    print = function(t, x, y, c)
      if x ~= nil and y == nil then
        c, x = x, nil -- print(text, color)
      end
      if x == nil then
        x, y = px_cur, py_cur
        py_cur = py_cur + 6
        if py_cur > 122 then py_cur = 122 end
      else
        px_cur, py_cur = 0, y + 6
      end
      return _pp(tostr(t), x, y, c)
    end
  end
  -- take effect immediately: top-level cart code below draws before _init runs
  camera()
  clip()
end
-- environment-swap support: `_ENV = t` keeps built-ins reachable via a fallback metatable
__envwrap = function(t)
  if type(t) == "table" and getmetatable(t) == nil then
    setmetatable(t, { __index = _G })
  end
  return t
end
-- 16.16 fixed-point bitwise helpers: the dialect's operators work on fractional values
-- (masks like 0x3fff.ffff are idiomatic), so everything routes through a 32-bit fixed
-- representation. The translator rewrites infix &, |, ~, <<, >> into these calls.
do
  local FLOOR = math.floor
  local function tofx(v)
    if type(v) == "string" then v = tonum(v) end
    return FLOOR(v * 65536) % 0x100000000
  end
  local function fromfx(u)
    if u >= 0x80000000 then u = u - 0x100000000 end
    return u / 65536
  end
  band = function(a, b) return fromfx(tofx(a) & tofx(b)) end
  bor  = function(a, b) return fromfx(tofx(a) | tofx(b)) end
  bxor = function(a, b) return fromfx(tofx(a) ~ tofx(b)) end
  bnot = function(a) return fromfx((~tofx(a)) % 0x100000000) end
  shl  = function(a, b) return fromfx(tofx(a * 2 ^ FLOOR(b))) end
  shr  = function(a, b) return FLOOR(a * 65536 / 2 ^ FLOOR(b)) / 65536 end -- arithmetic
  lshr = function(a, b) return fromfx(tofx(a) >> FLOOR(b)) end             -- logical
end
printh = function() end
load = function(n) __p8_load(tostr(n or "")) end -- multi-cart: swap to another cart
cursor = function(x, y) __p8_cursor(x, y) end
color = function() end
fillp = function(p) __fillp_p8(p) end
sspr_p8 = sspr
-- end of prelude ------------------------------------------------------------
)lua";

        constexpr const char* kEpilogue = R"lua(
-- p8 ext epilogue: keep everything inside the viewport, redraw the badge frame
do
  local i, d, u = _init, _draw, _update
  _init = function() __p8.setup() if i then i() end end
  if d then _draw = function() d() __p8.frame() end end
end
)lua";

        // ---------------- asset conversion ----------------
    } // namespace

    // Decode the p8 audio region (music table at +0, sfx table at +0x100) into a SoundBank.
    // Public because the runtime re-decodes it whenever a cart pokes audio RAM and then calls
    // sfx()/music() - some carts ship their own tracker that writes this memory directly.
    void decode_audio_ram(const unsigned char* ram, SoundBank& sounds)
    {
        // sfx: 68 bytes each = 32 notes x 2 + mode/speed/loop-start/loop-end
        for (int i = 0; i < 64; ++i)
        {
            const unsigned char* s   = ram + 0x100 + static_cast<size_t>(i) * 68;
            SfxPattern&          pat = sounds.sfx[static_cast<size_t>(i)];
            const int            spd = s[65];
            // The p8 tick is 183 samples at 22050 Hz (~1/120.5 s) - fake-08's constant - which
            // is our own 1/120 s tick to within 0.4%. Speeds therefore map 1:1.
            pat.speed      = static_cast<u8>(std::clamp(spd, 1, 255));
            pat.loop_start = static_cast<u8>(s[66] & 0x3f);
            pat.loop_end   = static_cast<u8>(s[67] & 0x3f);
            for (int nIdx = 0; nIdx < 32; ++nIdx)
            {
                const unsigned w = static_cast<unsigned>(s[nIdx * 2]) |
                                   (static_cast<unsigned>(s[nIdx * 2 + 1]) << 8);
                const int pitch  = static_cast<int>(w & 0x3f);
                const int wave   = static_cast<int>((w >> 6) & 0x7);
                const int vol    = static_cast<int>((w >> 9) & 0x7);
                const int effect = static_cast<int>((w >> 12) & 0x7);
                // 1:1 now that the synth has the full waveform set
                static const Wave kWave[8] = {Wave::Triangle, Wave::TiltedSaw, Wave::Saw,
                                              Wave::Square,   Wave::Pulse,     Wave::Organ,
                                              Wave::Noise,    Wave::Phaser};
                SfxNote& note = pat.notes[static_cast<size_t>(nIdx)];
                note.pitch    = static_cast<u8>(std::clamp(pitch, 0, 63)); // same scale: key 33 = 440 Hz on both
                note.wave     = static_cast<u8>(kWave[wave]);
                note.vol      = static_cast<u8>(vol);
                note.effect   = static_cast<u8>(effect);
            }
        }

        // music: 4 channel bytes; flags packed in bits 7 of bytes 0-2
        for (int i = 0; i < 64; ++i)
        {
            const unsigned char* m   = ram + static_cast<size_t>(i) * 4;
            MusicPattern&        pat = sounds.music[static_cast<size_t>(i)];
            pat.flags                = 0;
            if (m[0] & 0x80)
                pat.flags |= MusicPattern::kLoopStart;
            if (m[1] & 0x80)
                pat.flags |= MusicPattern::kLoopEnd;
            if (m[2] & 0x80)
                pat.flags |= MusicPattern::kStop;
            for (int c = 0; c < 4; ++c)
            {
                const int v = m[c] & 0x7f;
                pat.sfx[static_cast<size_t>(c)] = (v < 64) ? static_cast<u8>(v) : 255;
            }
        }
    }

    namespace
    {
        void convert_assets(const std::vector<unsigned char>& rom, std::string& gffTable,
                            SpriteSheet& sheet, Map& map, SoundBank& sounds)
        {
            sheet.clear();
            map.clear();
            sounds.clear();

            // gfx: two 4-bit pixels per byte, low nibble first, into the sheet's top-left 128x128
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; x += 2)
                {
                    const unsigned char b = rom[static_cast<size_t>(y) * 64 + x / 2];
                    sheet.set(x, y, static_cast<u8>(b & 0xf));
                    sheet.set(x + 1, y, static_cast<u8>(b >> 4));
                }

            // map: upper 32 rows at 0x2000, lower 32 rows shared with the gfx second half
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 128; ++x)
                {
                    const size_t src = (y < 32) ? 0x2000 + static_cast<size_t>(y) * 128 + x
                                                : 0x1000 + static_cast<size_t>(y - 32) * 128 + x;
                    map.set(x, y, rom[src]);
                }

            // gff -> sprite flags (the native VM reads sheet.flags), and a Lua table literal
            // for the transpiler prelude's fget/fset.
            gffTable = "__p8_gff={";
            for (int i = 0; i < 256; ++i)
            {
                const u8 f = rom[0x3000 + static_cast<size_t>(i)];
                sheet.set_flags(i, f);
                gffTable += std::to_string(static_cast<int>(f));
                if (i != 255)
                    gffTable += ',';
            }
            gffTable += "}\n";

            decode_audio_ram(rom.data() + 0x3100, sounds);
        }

        std::string assemble(const std::string& gffTable, const std::string& cartCode)
        {
            std::string out;
            out.reserve(cartCode.size() + 4096);
            out += gffTable;
            out += kPrelude;
            out += translate(cartCode);
            out += kEpilogue;
            if (const char* dump = std::getenv("LZ100_P8_DUMP")) // debug: write the assembled program
                std::ofstream(dump, std::ios::binary) << out;
            return out;
        }

        // ---------------- text form parsing ----------------

        bool parse_text(const std::string& text, std::vector<unsigned char>& rom, std::string& code)
        {
            rom.assign(kRomSize, 0);
            std::istringstream in(text);
            std::string        line, section;
            int                row = 0;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.rfind("__", 0) == 0 && line.size() > 4 && line.compare(line.size() - 2, 2, "__") == 0)
                {
                    section = line;
                    row     = 0;
                    continue;
                }
                if (section == "__lua__")
                {
                    code += line;
                    code += '\n';
                }
                else if (section == "__gfx__" && row < 128)
                {
                    for (int x = 0; x < 128 && x < static_cast<int>(line.size()); ++x)
                    {
                        const int v = hexval(line[static_cast<size_t>(x)]);
                        if (v < 0)
                            continue;
                        unsigned char& b = rom[static_cast<size_t>(row) * 64 + x / 2];
                        b = (x & 1) ? static_cast<unsigned char>(b | (v << 4))
                                    : static_cast<unsigned char>((b & 0xf0) | v);
                    }
                    ++row;
                }
                else if (section == "__map__" && row < 32)
                {
                    for (int x = 0; x < 128 && 2 * x + 1 < static_cast<int>(line.size()); ++x)
                    {
                        const int h = hexval(line[static_cast<size_t>(2 * x)]);
                        const int l = hexval(line[static_cast<size_t>(2 * x) + 1]);
                        if (h >= 0 && l >= 0)
                            rom[0x2000 + static_cast<size_t>(row) * 128 + x] =
                                static_cast<unsigned char>(h * 16 + l);
                    }
                    ++row;
                }
                else if (section == "__gff__" && row < 2)
                {
                    for (int x = 0; x < 128 && 2 * x + 1 < static_cast<int>(line.size()); ++x)
                    {
                        const int h = hexval(line[static_cast<size_t>(2 * x)]);
                        const int l = hexval(line[static_cast<size_t>(2 * x) + 1]);
                        if (h >= 0 && l >= 0)
                            rom[0x3000 + static_cast<size_t>(row) * 128 + x] =
                                static_cast<unsigned char>(h * 16 + l);
                    }
                    ++row;
                }
                else if (section == "__sfx__" && row < 64)
                {
                    // 8 hex header chars (mode, speed, loop start, loop end) + 32 x 5-char notes
                    const auto hx = [&](size_t i) -> int
                    { return i < line.size() ? hexval(line[i]) : 0; };
                    unsigned char* s = rom.data() + 0x3200 + static_cast<size_t>(row) * 68;
                    s[64]            = static_cast<unsigned char>(hx(0) * 16 + hx(1)); // editor mode
                    s[65]            = static_cast<unsigned char>(hx(2) * 16 + hx(3)); // speed
                    s[66]            = static_cast<unsigned char>(hx(4) * 16 + hx(5)); // loop start
                    s[67]            = static_cast<unsigned char>(hx(6) * 16 + hx(7)); // loop end
                    for (int nIdx = 0; nIdx < 32; ++nIdx)
                    {
                        const size_t o      = 8 + static_cast<size_t>(nIdx) * 5;
                        const int    pitch  = hx(o) * 16 + hx(o + 1);
                        const int    wave   = hx(o + 2);
                        const int    vol    = hx(o + 3);
                        const int    effect = hx(o + 4);
                        const unsigned w    = static_cast<unsigned>(pitch & 0x3f) |
                                           (static_cast<unsigned>(wave & 0x7) << 6) |
                                           (static_cast<unsigned>(vol & 0x7) << 9) |
                                           (static_cast<unsigned>(effect & 0x7) << 12);
                        s[nIdx * 2]     = static_cast<unsigned char>(w & 0xff);
                        s[nIdx * 2 + 1] = static_cast<unsigned char>(w >> 8);
                    }
                    ++row;
                }
                else if (section == "__music__" && row < 64)
                {
                    // "ff aabbccdd": flags byte, space, four channel bytes
                    const auto hx = [&](size_t i) -> int
                    { return i < line.size() ? hexval(line[i]) : -1; };
                    if (hx(0) >= 0 && line.size() >= 11)
                    {
                        const int      flags = hx(0) * 16 + hx(1);
                        unsigned char* m     = rom.data() + 0x3100 + static_cast<size_t>(row) * 4;
                        for (int c = 0; c < 4; ++c)
                        {
                            const size_t o = 3 + static_cast<size_t>(c) * 2;
                            int          v = hx(o) * 16 + hx(o + 1);
                            if (v < 0)
                                v = 0x41 + c; // silent
                            m[c] = static_cast<unsigned char>(v & 0x7f);
                        }
                        if (flags & 0x1)
                            m[0] |= 0x80;
                        if (flags & 0x2)
                            m[1] |= 0x80;
                        if (flags & 0x4)
                            m[2] |= 0x80;
                    }
                    ++row;
                }
            }
            return !code.empty();
        }
    } // namespace

    bool is_text_cart(const std::string& text) { return text.rfind("pico-8 cartridge", 0) == 0; }

    bool is_png_cart(const std::string& path)
    {
        int w = 0, h = 0, n = 0;
        if (!stbi_info(path.c_str(), &w, &h, &n))
            return false;
        return w == 160 && h == 205;
    }

    bool import_text(const std::string& text, std::string& code, SpriteSheet& sheet, Map& map,
                     SoundBank& sounds, std::vector<unsigned char>* romOut, std::string* rawOut)
    {
        if (!is_text_cart(text))
            return false;
        std::vector<unsigned char> rom;
        std::string                cartCode;
        if (!parse_text(text, rom, cartCode))
            return false;
        std::string gff;
        convert_assets(rom, gff, sheet, map, sounds);
        code = assemble(gff, cartCode);
        LZ_INFO("p8: text cart imported (%d code bytes)", static_cast<int>(cartCode.size()));
        if (romOut)
            *romOut = std::move(rom);
        if (rawOut)
            *rawOut = cartCode;
        return true;
    }

    bool import_png(const std::string& path, std::string& code, SpriteSheet& sheet, Map& map,
                    SoundBank& sounds, std::vector<unsigned char>* romOut, std::string* rawOut)
    {
        int            w = 0, h = 0, n = 0;
        unsigned char* img = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!img)
            return false;
        if (w != 160 || h != 205)
        {
            stbi_image_free(img);
            return false;
        }
        // one ROM byte per pixel: the low 2 bits of each channel, ordered A R G B
        std::vector<unsigned char> rom(kRomSize, 0);
        const size_t               px = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < px && i < kRomSize; ++i)
        {
            const unsigned char* p = img + i * 4; // stb gives R,G,B,A
            rom[i] = static_cast<unsigned char>(((p[3] & 3) << 6) | ((p[0] & 3) << 4) |
                                                ((p[1] & 3) << 2) | (p[2] & 3));
        }
        stbi_image_free(img);

        const std::string cartCode = extract_code(rom);
        if (cartCode.empty())
        {
            LZ_ERROR("p8: %s: could not extract code", path.c_str());
            return false;
        }
        std::string gff;
        convert_assets(rom, gff, sheet, map, sounds);
        code = assemble(gff, cartCode);
        LZ_INFO("p8: png cart imported (%d code bytes)", static_cast<int>(cartCode.size()));
        if (romOut)
            *romOut = std::move(rom);
        if (rawOut)
            *rawOut = cartCode;
        return true;
    }
} // namespace lazy100::p8
