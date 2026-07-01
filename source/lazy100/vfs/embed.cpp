#include "lazy100/vfs/embed.hpp"

// Extract the built-in assets linked into the executable. On Windows they live in an .rc
// RCDATA resource; on Unix they're .incbin'd symbols (see embed/lazy100.rc / lazy100.S,
// added to the host binary target). Resource id 1001 = the font.

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace lazy100::embed
{
    std::pair<const std::byte*, std::size_t> builtin_font()
    {
        HMODULE mod = ::GetModuleHandleW(nullptr);
        HRSRC   res = ::FindResourceW(mod, MAKEINTRESOURCEW(1001), reinterpret_cast<LPCWSTR>(RT_RCDATA));
        if (!res)
            return {nullptr, 0};
        HGLOBAL loaded = ::LoadResource(mod, res);
        const auto* ptr = static_cast<const std::byte*>(::LockResource(loaded));
        const DWORD size = ::SizeofResource(mod, res);
        return {ptr, static_cast<std::size_t>(size)};
    }
} // namespace lazy100::embed

#else

extern "C" const std::byte lazy100_font_start[];
extern "C" const std::byte lazy100_font_end[];

namespace lazy100::embed
{
    std::pair<const std::byte*, std::size_t> builtin_font()
    {
        return {lazy100_font_start, static_cast<std::size_t>(lazy100_font_end - lazy100_font_start)};
    }
} // namespace lazy100::embed

#endif
