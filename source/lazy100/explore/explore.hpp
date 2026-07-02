#pragma once

#include <memory>

namespace lazy100
{
    class Console;
    class Framebuffer;

    // The in-console game browser (shell `explore`): fetches the Lazy-100-games catalog
    // (games.json), shows a list with 320x240 preview images, downloads carts into carts/,
    // and keeps a local favorites list. All networking is polled net::Fetch - no blocking.
    class ExploreHost
    {
    public:
        ExploreHost();
        ~ExploreHost();

        ExploreHost(const ExploreHost&)            = delete;
        ExploreHost& operator=(const ExploreHost&) = delete;

        void update(Console& con);
        void draw(Console& con, Framebuffer& fb);

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100
