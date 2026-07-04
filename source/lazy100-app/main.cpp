// Lazy-100 host entry point. `xmake run lazy100 [cart.lua]`.
// With a cart path it loads and runs it; with none it shows the test pattern.
#include "lazy100/console/console.hpp"

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>

namespace
{
    lazy100::Console* g_console = nullptr; // set once boot() succeeds; valid for the page's life
}

// Boot a cart straight into Running from JS: the site writes the fetched .lz100.png bytes into
// MEMFS (FS.writeFile) then ccalls this with the path, so a clicked catalog cart plays in place
// with no shell load/run step. Returns 1 on success. The click itself is the user gesture that
// resumes the AudioContext (see the window handlers in audio.cpp).
extern "C" EMSCRIPTEN_KEEPALIVE int lazy100_boot_cart(const char* path)
{
    if (!g_console || !path)
        return 0;
    return g_console->restart_with_cart(path) ? 1 : 0; // replays the boot splash, then runs the cart
}

// Arm a cart for the "press a key to start" gate (the home page): the gate's gesture unlocks the
// audio, then starts this cart. Returns 1 on success.
extern "C" EMSCRIPTEN_KEEPALIVE int lazy100_arm_cart(const char* path)
{
    if (!g_console || !path)
        return 0;
    return g_console->arm_cart(path) ? 1 : 0;
}

// Virtual gamepad (touch): the site sets the held-button mask when a pad button goes down/up.
// Bits match Input::Button (Left=1, Right=2, Up=4, Down=8, O=16, X=32); poll() ORs it into held.
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_set_pad(int mask)
{
    if (g_console)
        g_console->input().set_touch(static_cast<unsigned>(mask) & 0x3f);
}

// Virtual keyboard (touch): OR a Keyboard::Key bitmask (1u<<Key) into the held keys so the touch
// gamepad / on-screen keyboard drive menus, the shell and editors like a real keyboard. Bit order:
// Escape=0, Return=1, Backspace=2, Delete=3, Tab=4, Left=5, Right=6, Up=7, Down=8, Home=9, End=10,
// PageUp=11, PageDown=12, Num7=13.
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_set_keys(int mask)
{
    if (g_console)
        g_console->keyboard().inject_keys(static_cast<unsigned>(mask));
}

// Virtual keyboard typed text: append UTF-8 characters as if typed (for the on-screen keyboard).
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_type_text(const char* utf8)
{
    if (g_console && utf8)
        g_console->keyboard().inject_text(utf8);
}

// Current console mode, so the site can show the right touch controls (2 = Running/run-cart ->
// gamepad; everything else -> mouse + keyboard). Matches ConsoleMode: Boot=0 Shell=1 Running=2
// Editor=3 Explore=4.
extern "C" EMSCRIPTEN_KEEPALIVE int lazy100_mode()
{
    return g_console ? static_cast<int>(g_console->mode()) : 0;
}

// Kiosk mode: the play-only home page hides the developer pause-menu items (edit/explore/shell).
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_set_kiosk(int on)
{
    if (g_console)
        g_console->set_kiosk(on != 0);
}

// Auto-pause when the tab goes to background: opens the cart pause menu (as ESC would), which
// also pauses the music. Safe to call any time — no-op unless a cart is running menu-less.
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_pause()
{
    if (g_console)
        g_console->pause_from_web();
}

// Re-warm the audio device after the tab returns to the foreground: the JS side resumes the
// suspended AudioContext, and this re-arms the ~1.5s sub-audible dither so a speaker that went
// back to power-saving wakes up before the music resumes (instead of swallowing the first notes).
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_warm_audio()
{
    if (g_console)
        g_console->audio().rewarm();
}

// Background/foreground audio-device lifecycle: iOS revokes the audio session in the background,
// so the site kills the device on hide and rebuilds it (fresh AudioContext) on return.
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_audio_suspend()
{
    if (g_console)
        g_console->audio().device_suspend();
}
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_audio_resume()
{
    if (g_console)
        g_console->audio().device_resume();
}

// Virtual mouse (the console screen used as a trackpad): x/y are framebuffer coords (0..319 / 0..239),
// `buttons` bit0 = left, bit1 = right, bit2 = middle. Pass buttons < 0 to release the injection and
// hand control back to the real pointer.
extern "C" EMSCRIPTEN_KEEPALIVE void lazy100_set_mouse(int x, int y, int buttons)
{
    if (!g_console)
        return;
    if (buttons < 0)
        g_console->mouse().inject(0, 0, 0, false);
    else
        g_console->mouse().inject(x, y, static_cast<unsigned>(buttons), true);
}
#endif

int main(int argc, char** argv)
{
    const char* cart = argc > 1 ? argv[1] : nullptr;

    lazy100::Console console;
    if (!console.boot(cart))
        return 1;
#if defined(__EMSCRIPTEN__)
    g_console = &console; // run() never returns on web (simulated infinite loop), so this stays valid
#endif
    console.run();
    console.shutdown();
    return 0;
}
