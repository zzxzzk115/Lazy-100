// cartwav: headless music render of a cart -> 16-bit stereo WAV.
// Usage: cartwav <cart> <out.wav> [pattern] [seconds]
// Analysis aid for the audio engine: lets pattern seams, clicks and dropouts be inspected
// with real tools (spectrograms, RMS windows) instead of by ear alone.

#include "lazy100/audio/audio.hpp"
#include "lazy100/console/console.hpp"
#include "lazy100/vfs/vfs.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: cartwav <cart> <out.wav> [pattern] [seconds]\n");
        return 2;
    }
    const std::string cart = argv[1];
    const std::string out  = argv[2];
    const int         pat  = argc > 3 ? std::atoi(argv[3]) : 0;
    const double      secs = argc > 4 ? std::atof(argv[4]) : 15.0;

    lazy100::vfs::init();
    auto con = std::make_unique<lazy100::Console>(); // big object: keep it off the stack
    if (!con->load_cart_file(cart))
    {
        std::fprintf(stderr, "cartwav: cannot load %s\n", cart.c_str());
        return 1;
    }
    if (!lazy100::Audio::debug_render_music(con->sounds(), pat, secs, out))
    {
        std::fprintf(stderr, "cartwav: render failed\n");
        return 1;
    }
    std::printf("cartwav: %s pattern %d -> %s (%.1fs)\n", cart.c_str(), pat, out.c_str(), secs);
    return 0;
}
