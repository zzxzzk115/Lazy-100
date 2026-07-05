/* Unix (Linux/macOS): link built-in assets into the binary via .incbin, exposing
 * lazy100_font_start / lazy100_font_end symbols (see source/lazy100/vfs/embed.cpp).
 * Added to the host binary target on non-Windows platforms. Paths are relative to the
 * project root (the assembler's working dir). */

#if defined(__APPLE__)
.section __DATA,__lazy100
.globl _lazy100_font_start
.globl _lazy100_font_end
.p2align 4
_lazy100_font_start:
.incbin "assets/fonts/fusion-pixel-8px-monospaced-zh_hans.ttf"
_lazy100_font_end:
#else
/* %progbits, not @progbits: '@' starts a comment in 32-bit ARM assembly. */
.section .rodata.lazy100,"a",%progbits
.globl lazy100_font_start
.globl lazy100_font_end
.p2align 4
lazy100_font_start:
.incbin "assets/fonts/fusion-pixel-8px-monospaced-zh_hans.ttf"
lazy100_font_end:
/* Mark the stack non-executable; without this note new GNU ld fails the link. */
.section .note.GNU-stack,"",%progbits
#endif
