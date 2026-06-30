#pragma once

#include <cstdarg>
#include <cstdio>

// Minimal logging for the kernel. Also the sink that gpu/present routes VRI's
// MessageCallback diagnostics through, so backend errors print instead of vanishing.
namespace lazy100::detail
{
    inline void log_line(const char* level, std::FILE* out, const char* fmt, ...)
    {
        std::va_list ap;
        va_start(ap, fmt);
        std::fprintf(out, "[lazy100][%s] ", level);
        std::vfprintf(out, fmt, ap);
        std::fputc('\n', out);
        va_end(ap);
        std::fflush(out);
    }
} // namespace lazy100::detail

#define LZ_INFO(...)  ::lazy100::detail::log_line("INFO", stdout, __VA_ARGS__)
#define LZ_WARN(...)  ::lazy100::detail::log_line("WARN", stderr, __VA_ARGS__)
#define LZ_ERROR(...) ::lazy100::detail::log_line("ERROR", stderr, __VA_ARGS__)
