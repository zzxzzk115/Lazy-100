#pragma once

namespace lazy100::vfs
{
    // Flush pending filesystem writes to durable storage. Desktop writes are already durable
    // (real files), so this is a no-op there; on the web it schedules FS.syncfs(false) so
    // carts/, saves/ and favorites survive a page reload (IDBFS -> IndexedDB).
    void persist_flush();

    // Offer `path` to the user as a browser file download (named after its basename). Desktop
    // no-op: the file already lives on the real filesystem. Used after saving a cart on the
    // web so .lz100/.png exports leave the sandboxed filesystem.
    void offer_download(const char* path);
} // namespace lazy100::vfs
