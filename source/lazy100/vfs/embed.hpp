#pragma once

#include <cstddef>
#include <utility>

namespace lazy100::embed
{
    // Bytes of a built-in asset linked into the binary (Windows .rc RCDATA / Unix .incbin).
    // The memory stays valid for the whole process; returns {nullptr,0} if the resource is
    // missing. vfs copies these into a MemoryFileSystem at startup.
    std::pair<const std::byte*, std::size_t> builtin_font();
} // namespace lazy100::embed
