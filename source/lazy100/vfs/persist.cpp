#include "lazy100/vfs/persist.hpp"

#if defined(__EMSCRIPTEN__)
#    include <emscripten.h>

// clang-format off
EM_JS(void, lz100_syncfs, (), {
    if (Module.lz100SyncPending) return;      /* debounce: one sync per event-loop turn */
    Module.lz100SyncPending = true;
    setTimeout(function () {
        Module.lz100SyncPending = false;
        FS.syncfs(false, function (err) {
            if (err) console.warn("lazy100: FS.syncfs failed:", err);
        });
    }, 0);
});

/* Read `path` out of the virtual filesystem and hand it to the browser as a download. */
EM_JS(void, lz100_download, (const char* pathPtr), {
    try {
        var path = UTF8ToString(pathPtr);
        var name = path.substring(path.lastIndexOf("/") + 1);
        var data = FS.readFile(path); /* Uint8Array */
        var blob = new Blob([data], { type: "application/octet-stream" });
        var url  = URL.createObjectURL(blob);
        var a    = document.createElement("a");
        a.href = url;
        a.download = name;
        document.body.appendChild(a);
        a.click();
        setTimeout(function () {
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }, 0);
    } catch (e) { console.warn("lazy100: download failed:", e); }
});
EM_JS_DEPS(lz100_persist_deps, "$UTF8ToString");
// clang-format on

namespace lazy100::vfs
{
    void persist_flush() { lz100_syncfs(); }
    void offer_download(const char* path) { lz100_download(path); }
} // namespace lazy100::vfs

#else

namespace lazy100::vfs
{
    void persist_flush() {}
    void offer_download(const char*) {}
} // namespace lazy100::vfs

#endif
