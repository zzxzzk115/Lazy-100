#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lazy100::vfs
{
    // Virtual filesystem for the console's assets. init() populates an in-memory backend
    // (vfilesystem::MemoryFileSystem) from the assets linked into the binary, so the exe is
    // self-contained. Disk-backed access (for user carts) joins in M7.
    bool init();

    // Read a built-in asset's bytes by logical path (e.g. "fonts/fusion.ttf").
    std::optional<std::vector<std::byte>> read_builtin(const std::string& path);
} // namespace lazy100::vfs
