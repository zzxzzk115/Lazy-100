#include "lazy100/explore/explore.hpp"

#include "lazy100/cart/cartpng.hpp"
#include "lazy100/common/log.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/console/pausemenu.hpp"
#include "lazy100/net/fetch.hpp"
#include "lazy100/vfs/persist.hpp"
#include "lazy100/video/draw.hpp"
#include "lazy100/video/font.hpp"
#include "lazy100/video/framebuffer.hpp"
#include "lazy100/video/palette.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace lazy100
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr const char* kCatalogBase = "https://raw.githubusercontent.com/zzxzzk115/Lazy-100-games/main/";
        constexpr const char* kFavoritesPath = "saves/favorites.txt";

        // 320x240 catalog preview, quantized to the console palette for the indexed framebuffer.
        struct Preview
        {
            std::vector<u8> px; // kScreenW*kScreenH palette indices; empty = failed
        };

        struct Game
        {
            std::string              id, name, author, description, version, category;
            std::vector<std::string> tags;
            std::string              cartUrl, previewUrl;
        };

        // Keep the catalog cart's extension so load_cart_file picks the right importer: a PNG
        // cartridge (.lz100.png) is decoded as an image, a bare .lz100 as text.
        std::string local_cart_path(const Game& g)
        {
            const bool png = g.cartUrl.size() >= 4 && g.cartUrl.compare(g.cartUrl.size() - 4, 4, ".png") == 0;
            return "carts/" + g.id + (png ? ".lz100.png" : ".lz100");
        }

        // Nearest palette entry for an RGB color, memoized (previews have few unique colors).
        u8 nearest_index(const Palette& pal, u8 r, u8 g, u8 b, std::unordered_map<u32, u8>& memo)
        {
            const u32 key = static_cast<u32>(r) | static_cast<u32>(g) << 8 | static_cast<u32>(b) << 16;
            if (const auto it = memo.find(key); it != memo.end())
                return it->second;
            int best = 0, bestD = INT32_MAX;
            for (u32 i = 0; i < Palette::size(); ++i)
            {
                const Color32 c  = pal.default_at(i);
                const int     dr = static_cast<int>(c.r) - r, dg = static_cast<int>(c.g) - g,
                          db     = static_cast<int>(c.b) - b;
                const int d      = dr * dr + dg * dg + db * db;
                if (d < bestD)
                {
                    bestD = d;
                    best  = static_cast<int>(i);
                }
            }
            memo[key] = static_cast<u8>(best);
            return static_cast<u8>(best);
        }

        // Greedy word-wrap of `text` into lines no wider than maxW pixels.
        std::vector<std::string> wrap(const std::string& text, int maxW)
        {
            std::vector<std::string> lines;
            std::string              cur, word;
            const auto               flushWord = [&] {
                if (word.empty())
                    return;
                const std::string cand = cur.empty() ? word : cur + " " + word;
                if (font::text_width(cand.c_str()) <= maxW)
                    cur = cand;
                else
                {
                    if (!cur.empty())
                        lines.push_back(cur);
                    cur = word;
                }
                word.clear();
            };
            for (const char ch : text)
            {
                if (ch == ' ' || ch == '\n')
                {
                    flushWord();
                    if (ch == '\n')
                    {
                        lines.push_back(cur);
                        cur.clear();
                    }
                }
                else
                    word += ch;
            }
            flushWord();
            if (!cur.empty())
                lines.push_back(cur);
            return lines;
        }
    } // namespace

    struct ExploreHost::Impl
    {
        enum class Phase
        {
            Boot,       // nothing fetched yet: kick off the index fetch on first update
            FetchIndex, // games.json in flight
            Ready,      // list shown
            Error       // index fetch failed; R retries
        };

        Phase             phase = Phase::Boot;
        std::string       status;    // bottom status line
        std::string       error;
        std::vector<Game> games;
        int               sel    = 0;
        int               scroll = 0;

        net::Fetch indexFetch;
        net::Fetch previewFetch;
        std::string previewFetchingId;
        net::Fetch cartFetch;
        std::string cartFetchingId;
        std::string cartFetchTarget; // local path to write the download to (keeps the cart extension)

        // ESC pause-style popup (shared component): continue / shell / quit.
        PauseMenu menu;

        std::unordered_map<std::string, Preview> previews; // id -> quantized image (may be empty=failed)
        std::unordered_map<u32, u8>              quantMemo;
        std::set<std::string>                    favorites;
        bool                                     favoritesLoaded = false;

        // ---- favorites ----
        void load_favorites()
        {
            if (favoritesLoaded)
                return;
            favoritesLoaded = true;
            std::ifstream f(kFavoritesPath);
            std::string   line;
            while (std::getline(f, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                    line.pop_back();
                if (!line.empty())
                    favorites.insert(line);
            }
        }

        void save_favorites()
        {
            std::error_code ec;
            fs::create_directories("saves", ec);
            std::ofstream f(kFavoritesPath, std::ios::trunc);
            for (const std::string& id : favorites)
                f << id << '\n';
            f.close();
            vfs::persist_flush();
        }

        // ---- catalog ----
        void start_index_fetch()
        {
            phase  = Phase::FetchIndex;
            status = "fetching catalog...";
            indexFetch.start(std::string(kCatalogBase) + "games.json");
        }

        void parse_index(const std::vector<u8>& bytes)
        {
            games.clear();
            try
            {
                const auto j = nlohmann::json::parse(bytes.begin(), bytes.end());
                if (j.value("kind", "") != "lazy100.gamesCatalog")
                    throw std::runtime_error("not a lazy100 games catalog");
                for (const auto& e : j.at("games"))
                {
                    Game g;
                    g.id          = e.at("id").get<std::string>();
                    g.name        = e.value("name", g.id);
                    g.author      = e.value("author", "");
                    g.description = e.value("description", "");
                    g.version     = e.value("version", "");
                    g.category    = e.value("category", "");
                    if (e.contains("tags"))
                        for (const auto& t : e["tags"])
                            g.tags.push_back(t.get<std::string>());
                    g.cartUrl = kCatalogBase + e.at("cart").get<std::string>();
                    // A PNG cartridge doubles as its own preview, so `preview` is optional now:
                    // fall back to the cart image (decode_preview nearest-samples any size).
                    g.previewUrl = kCatalogBase + (e.contains("preview") ? e.at("preview").get<std::string>()
                                                                          : e.at("cart").get<std::string>());
                    games.push_back(std::move(g));
                }
                phase  = Phase::Ready;
                status = std::to_string(games.size()) + " game(s)";
                sel    = std::clamp(sel, 0, std::max(0, static_cast<int>(games.size()) - 1));
            }
            catch (const std::exception& ex)
            {
                phase = Phase::Error;
                error = std::string("catalog parse failed: ") + ex.what();
                LZ_ERROR("explore: %s", error.c_str());
            }
        }

        // ---- previews ----
        void decode_preview(const std::string& id, const std::vector<u8>& bytes, const Palette& pal)
        {
            Preview pv;
            int     w = 0, h = 0, n = 0;
            if (unsigned char* img = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                           &w, &h, &n, 4))
            {
                // A .lz100.png cartridge holds the 320x240 screenshot at a fixed offset within a
                // larger card: crop just that region for the preview. Any other image (a distinct
                // preview) is nearest-sampled whole onto the 320x240 grid.
                const bool isCart = w == cartpng::kShotW + 2 * cartpng::kShotX && h >= cartpng::kShotY + cartpng::kShotH;
                const int  ox = isCart ? cartpng::kShotX : 0;
                const int  oy = isCart ? cartpng::kShotY : 0;
                const int  sw = isCart ? cartpng::kShotW : w;
                const int  sh = isCart ? cartpng::kShotH : h;
                pv.px.resize(Framebuffer::pixel_count());
                for (u32 y = 0; y < kScreenH; ++y)
                    for (u32 x = 0; x < kScreenW; ++x)
                    {
                        const u32            sx = ox + x * static_cast<u32>(sw) / kScreenW;
                        const u32            sy = oy + y * static_cast<u32>(sh) / kScreenH;
                        const unsigned char* p  = &img[(static_cast<size_t>(sy) * w + sx) * 4];
                        pv.px[y * kScreenW + x] = nearest_index(pal, p[0], p[1], p[2], quantMemo);
                    }
                stbi_image_free(img);
            }
            previews[id] = std::move(pv); // empty px = decode failure, drawn as "no preview"
        }

        // ---- per-frame ----
        void update(Console& con)
        {
            load_favorites();

            if (phase == Phase::Boot)
                start_index_fetch();

            if (indexFetch.done() && phase == Phase::FetchIndex)
            {
                if (indexFetch.ok())
                    parse_index(indexFetch.data());
                else
                {
                    phase = Phase::Error;
                    error = "catalog fetch failed: " + indexFetch.error();
                }
            }

            Keyboard& kb = con.keyboard();

            // ESC toggles the popup menu; while it is open it swallows all input.
            if (kb.pressed(Keyboard::Escape))
            {
                if (menu.is_open())
                    menu.close();
                else
                {
                    std::vector<std::string> items {"continue", "back to shell"};
#if !defined(__EMSCRIPTEN__)
                    items.emplace_back("exit console"); // meaningless inside a browser tab
#endif
                    menu.open(std::move(items));
                }
                return;
            }
            if (menu.is_open())
            {
                switch (menu.update(kb))
                {
                    case 1: con.set_mode(ConsoleMode::Shell); break;
                    case 2: con.quit(); break;
                    default: break; // 0 = continue (menu closed itself), -1 = still open
                }
                return;
            }

            if (phase == Phase::Error || phase == Phase::Ready)
            {
                if (kb.text().find('r') != std::string::npos && !indexFetch.active())
                    start_index_fetch();
            }
            if (phase != Phase::Ready || games.empty())
                return;

            // selection
            if (kb.repeat(Keyboard::Up))
                sel = std::max(0, sel - 1);
            if (kb.repeat(Keyboard::Down))
                sel = std::min(static_cast<int>(games.size()) - 1, sel + 1);
            sel -= con.mouse().wheel(); // wheel up = -? (wheel>0 scrolls up)
            sel = std::clamp(sel, 0, static_cast<int>(games.size()) - 1);

            const Game& g = games[static_cast<size_t>(sel)];

            // favorite toggle
            if (kb.text().find('f') != std::string::npos)
            {
                if (!favorites.erase(g.id))
                    favorites.insert(g.id);
                save_favorites();
            }

            // lazy preview fetch for the selected game
            if (!previews.contains(g.id) && !previewFetch.active() && previewFetchingId.empty())
            {
                previewFetchingId = g.id;
                previewFetch.start(g.previewUrl);
            }
            if (previewFetch.done() && !previewFetchingId.empty())
            {
                if (previewFetch.ok())
                    decode_preview(previewFetchingId, previewFetch.data(), con.palette());
                else
                    previews[previewFetchingId] = Preview {}; // failed: cache the miss
                previewFetchingId.clear();
            }

            // cart download completion
            if (cartFetch.done() && !cartFetchingId.empty())
            {
                if (cartFetch.ok())
                {
                    std::error_code ec;
                    fs::create_directories("carts", ec);
                    std::ofstream out(cartFetchTarget, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char*>(cartFetch.data().data()),
                              static_cast<std::streamsize>(cartFetch.data().size()));
                    out.close();
                    vfs::persist_flush();
                    status = cartFetchingId + " downloaded - enter to play";
                }
                else
                    status = "download failed: " + cartFetch.error();
                cartFetchingId.clear();
            }

            // enter: play if local, otherwise download
            if (kb.pressed(Keyboard::Return))
            {
                const std::string local = local_cart_path(g);
                std::error_code   ec;
                if (fs::exists(local, ec))
                {
                    if (con.load_cart_file(local) && con.start_cart())
                        return; // mode switched to Running
                    status = "failed to start " + g.id;
                }
                else if (!cartFetch.active())
                {
                    cartFetchingId = g.id;
                    cartFetchTarget = local;
                    status          = "downloading " + g.id + "...";
                    cartFetch.start(g.cartUrl);
                }
            }
        }

        // ---- drawing ----
        // Layout: the selected game's 320x240 preview fills the whole screen 1:1 as the
        // backdrop; a slim header and a bottom panel (game list + metadata + key help) are
        // overlaid on top of it.
        void draw(Console& con, Framebuffer& fb)
        {
            const int W  = static_cast<int>(kScreenW);
            const int H  = static_cast<int>(kScreenH);
            const int lh = font::line_height();

            fb.cls(0);

            // full-screen preview backdrop
            if (phase == Phase::Ready && !games.empty())
            {
                const Game& g  = games[static_cast<size_t>(sel)];
                const auto  it = previews.find(g.id);
                if (it != previews.end() && !it->second.px.empty())
                {
                    const std::vector<u8>& px = it->second.px;
                    for (int y = 0; y < H; ++y)
                        for (int x = 0; x < W; ++x)
                            fb.pset(x, y, px[static_cast<size_t>(y) * kScreenW + static_cast<size_t>(x)]);
                }
                else
                {
                    const char* msg = (it == previews.end()) ? "loading preview..." : "no preview";
                    font::print(fb, msg, (W - font::text_width(msg)) / 2, H / 2 - 30, 5);
                }
            }
            else if (phase == Phase::Error)
            {
                for (int i = 0; const std::string& l : wrap(error, W - 20))
                    font::print(fb, l.c_str(), 10, 30 + (i++) * lh, 8);
                font::print(fb, "press r to retry", 10, 30 + 3 * lh, 6);
            }
            else
                font::print(fb, "fetching catalog...", 10, 30, 6);

            // slim header over the preview
            fb.rectfill(0, 0, W - 1, lh + 1, 1);
            font::print(fb, "EXPLORE", 4, 1, 7);
            font::print(fb, "lazy-100 games", 4 + font::text_width("EXPLORE") + 6, 1, 13);
            const char* right = (phase == Phase::FetchIndex) ? "fetching..." : status.c_str();
            font::print(fb, right, W - 4 - font::text_width(right), 1, 6);

            if (phase == Phase::Ready && !games.empty())
                draw_panel(fb);

            menu.draw(fb); // no-op unless open

            (void)con;
        }

        void draw_panel(Framebuffer& fb)
        {
            const int W    = static_cast<int>(kScreenW);
            const int H    = static_cast<int>(kScreenH);
            const int lh   = font::line_height();
            const int rowH = lh + 1;

            // bottom panel: 4 list/info rows + a key-help footer
            constexpr int kRows = 4;
            const int     panelH = kRows * rowH + lh + 7;
            const int     y0     = H - panelH;
            fb.rectfill(0, y0 - 1, W - 1, y0 - 1, 5); // top border
            fb.rectfill(0, y0, W - 1, H - 1, 1);      // panel body

            // keep the selection inside the list window
            if (sel < scroll)
                scroll = sel;
            if (sel >= scroll + kRows)
                scroll = sel - kRows + 1;

            // left: game list
            const int listX = 4, listW = 140;
            for (int row = 0; row < kRows; ++row)
            {
                const int idx = scroll + row;
                if (idx >= static_cast<int>(games.size()))
                    break;
                const Game& g = games[static_cast<size_t>(idx)];
                const int   y = y0 + 3 + row * rowH;
                if (idx == sel)
                {
                    fb.rectfill(listX - 2, y - 1, listX + listW, y + lh - 1, 2);
                    font::print(fb, ">", listX, y, 10);
                }
                if (favorites.contains(g.id))
                    font::print(fb, "*", listX + 7, y, 10);
                std::error_code ec;
                const bool      local = std::filesystem::exists("carts/" + g.id + ".lz100", ec);
                font::print(fb, g.name.c_str(), listX + 15, y, idx == sel ? 7 : (local ? 6 : 13));
            }

            // right: selected game's metadata
            const Game& g  = games[static_cast<size_t>(sel)];
            const int   ix = listX + listW + 10;
            const int   iw = W - ix - 4;
            int         y  = y0 + 3;
            font::print(fb, g.name.c_str(), ix, y, 7);
            {
                std::error_code ec;
                const char*     state = std::filesystem::exists("carts/" + g.id + ".lz100", ec)
                                            ? "installed" : "online";
                font::print(fb, state, ix + iw - font::text_width(state), y, 11);
            }
            y += rowH;
            const std::string byline = "by " + g.author + "  v" + g.version + "  " + g.category;
            font::print(fb, byline.c_str(), ix, y, 13);
            y += rowH;
            if (!g.tags.empty())
            {
                std::string tags;
                for (const std::string& t : g.tags)
                    tags += (tags.empty() ? "#" : " #") + t;
                font::print(fb, tags.c_str(), ix, y, 12);
            }
            y += rowH;
            {
                // one line of description, ellipsized to the panel width
                std::string d = g.description;
                while (!d.empty() && font::text_width((d + "...").c_str()) > iw)
                    d.pop_back();
                if (d.size() < g.description.size())
                    d += "...";
                font::print(fb, d.c_str(), ix, y, 6);
            }

            // footer: key help
            font::print(fb, "up/down select  enter play  f fav  r refresh  esc back", 4,
                        H - lh - 2, 5);
        }
    };

    ExploreHost::ExploreHost() : p_(std::make_unique<Impl>()) {}
    ExploreHost::~ExploreHost() = default;

    void ExploreHost::update(Console& con) { p_->update(con); }
    void ExploreHost::draw(Console& con, Framebuffer& fb) { p_->draw(con, fb); }
} // namespace lazy100
