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

#elif defined(__EMSCRIPTEN__)

// wasm: there's no ELF/COFF data section to .incbin into, so the build bakes the font into
// MEMFS at /lazy100_font.ttf (--embed-file, see examples/xmake.lua) and we read it back once
// into a static buffer - mirroring the .rc/.S delivery on desktop.
#include <fstream>
#include <vector>

namespace lazy100::embed
{
    std::pair<const std::byte*, std::size_t> builtin_font()
    {
        static const std::vector<std::byte> data = []
        {
            std::ifstream in("/lazy100_font.ttf", std::ios::binary | std::ios::ate);
            if (!in)
                return std::vector<std::byte> {};
            const auto             size = static_cast<std::size_t>(in.tellg());
            std::vector<std::byte> buf(size);
            in.seekg(0);
            in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
            return buf;
        }();
        return {data.data(), data.size()};
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
