#include "lazy100/vfs/vfs.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/vfs/embed.hpp"

#include <vfilesystem/backends/memory_filesystem.hpp>

#include <memory>

namespace lazy100::vfs
{
    namespace
    {
        std::shared_ptr<vfilesystem::MemoryFileSystem> g_builtin;

        void put(const char* path, std::pair<const std::byte*, std::size_t> blob)
        {
            if (!blob.first || blob.second == 0)
            {
                LZ_WARN("vfs: embedded asset '%s' missing", path);
                return;
            }
            g_builtin->put(path, std::vector<std::byte>(blob.first, blob.first + blob.second));
        }
    } // namespace

    bool init()
    {
        g_builtin = std::make_shared<vfilesystem::MemoryFileSystem>();
        put("fonts/fusion-pixel-8px-monospaced-zh_hans.ttf", embed::builtin_font());
        return true;
    }

    std::optional<std::vector<std::byte>> read_builtin(const std::string& path)
    {
        if (!g_builtin)
            return std::nullopt;
        auto r = g_builtin->readAll(path);
        if (!r)
            return std::nullopt;
        return std::move(r).value();
    }
} // namespace lazy100::vfs
