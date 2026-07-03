// cartshot - render a cart's first frame headlessly and write it as a 320x240 PNG.
// Usage: cartshot <cart.lz100|cart.png|cart.lua> <out.png>
// Generates the preview images the Lazy-100-games catalog requires (exact palette colors,
// no window or GPU needed), so it also suits CI.
#include "lazy100/console/console.hpp"

#include <cstdio>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
// dbghelp AFTER windows.h
#    include <dbghelp.h>
#    pragma comment(lib, "dbghelp.lib")

static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep)
{
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proc, nullptr, TRUE);
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto pc   = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    std::fprintf(stderr, "\n=== CRASH: code 0x%08lx at %p (module base %p, RVA 0x%llx) ===\n",
                 ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
                 reinterpret_cast<void*>(base), static_cast<unsigned long long>(pc - base));

    CONTEXT*      ctx = ep->ContextRecord;
    STACKFRAME64  sf {};
    sf.AddrPC.Offset    = ctx->Rip;
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrFrame.Offset = ctx->Rbp;
    sf.AddrFrame.Mode   = AddrModeFlat;
    sf.AddrStack.Offset = ctx->Rsp;
    sf.AddrStack.Mode   = AddrModeFlat;

    char        symbuf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen   = 512;

    for (int i = 0; i < 30; ++i)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (sf.AddrPC.Offset == 0)
            break;
        DWORD64 disp = 0;
        DWORD   ldisp = 0;
        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof(line);
        const char* name = "??";
        if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym))
            name = sym->Name;
        if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ldisp, &line))
            std::fprintf(stderr, "  #%02d %s  (%s:%lu)\n", i, name, line.FileName, line.LineNumber);
        else
            std::fprintf(stderr, "  #%02d %s +0x%llx\n", i, name, disp);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char** argv)
{
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crash_handler);
#endif
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: cartshot <cart.lz100|cart.png|cart.lua> <out.png>\n");
        return 2;
    }
    lazy100::Console console;
    if (!console.headless_shot(argv[1], argv[2]))
    {
        std::fprintf(stderr, "cartshot: failed to render %s\n", argv[1]);
        return 1;
    }
    std::printf("cartshot: %s -> %s (320x240)\n", argv[1], argv[2]);
    return 0;
}
