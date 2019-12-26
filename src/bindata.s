#include "equates.h"

	.global font
	.global fontpal
	.text
 .align
 .pool

@This is removed from memory after it's loaded in Multiboot builds (except for builds including Link Transfer)
#if MULTIBOOT && COMPY
 .section .rdata, "ax", %progbits
#else
 .section .append, "ax", %progbits
#endif


font:
	.incbin "../src/font2.lz77"
fontpal:
	.incbin "../src/fontpal.bin"

#if (MULTIBOOT || GOMULTIBOOT) && !COMPY
	.global mb_binary
	.global mb_binary_end
mb_binary:
	.incbin "../pocketnes_mb.gba"
mb_binary_end:
#endif
