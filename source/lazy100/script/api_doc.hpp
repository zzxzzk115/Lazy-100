#pragma once

#include <string>

namespace lazy100::apidoc
{
    // The single source of truth for the console's Lua API surface: every function's signature
    // (with semantic parameter names, optional ones in [brackets]) and a one-line brief,
    // grouped by feature. Drives the code editor's parameter hints, its book-icon cheatsheet
    // overlay, autocomplete/syntax vocabularies, and mirrors docs/CHEATSHEET*.md.
    struct Fn
    {
        const char* name;  // bare function name ("spr")
        const char* sig;   // full signature ("spr(n, x, y, [w], [h], [flip_x], [flip_y])")
        const char* brief; // one-liner shown under the signature
    };

    struct Category
    {
        const char* name;
        const Fn*   fns;
        int         count;
    };

    // All categories, in cheatsheet order. `count` receives the category count.
    const Category* categories(int& count);

    // Signature lookup for the editor's parameter hint; nullptr if `name` is not API.
    const Fn* find(const std::string& name);
} // namespace lazy100::apidoc
