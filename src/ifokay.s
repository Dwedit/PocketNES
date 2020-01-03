#include "equates.h"

#if !COMPY

global_func if_okay
//global_func if_okay_2
global_func memcpy_if_okay
//global_func memmove_if_okay
global_func simpleswap_if_okay
//global_func swapmem_if_okay
//global_func assign_chr_pages_if_okay
//global_func assign_chr_pages2_if_okay
//global_func assign_prg_pages_if_okay
//global_func assign_prg_pages2_if_okay
//#if USE_GAME_SPECIFIC_HACKS
//global_func swap_prg_pages_if_okay
//#endif

.text
.thumb

//if_okay2:
//	movs r12,r3
//	ldr r3,=do_not_assign_pages
//	b if_okay_entry
if_okay:
	movs r12,r3
	ldr r3,=do_not_decompress
if_okay_entry:
	ldrb r3,[r3]
	movs r3,r3
	bne not_okay
	bx r12
not_okay:
	bx lr

memcpy_if_okay:
	ldr r3,=memcpy32
	b if_okay
//memmove_if_okay:
//	ldr r3,=memmove32
//	b if_okay
simpleswap_if_okay:
	ldr r3,=simpleswap32
	b if_okay
//swapmem_if_okay:
//	ldr r3,=swapmem
//	b if_okay

//assign_chr_pages_if_okay:
//	ldr r3,=assign_chr_pages
//	b if_okay2
//assign_chr_pages2_if_okay:
//	ldr r3,=assign_chr_pages2
//	b if_okay2
//assign_prg_pages_if_okay:
//	ldr r3,=assign_prg_pages
//	b if_okay2
//assign_prg_pages2_if_okay:
//	ldr r3,=assign_prg_pages2
//	b if_okay2
//#if USE_GAME_SPECIFIC_HACKS
//swap_prg_pages_if_okay:
//	ldr r3,=swap_prg_pages
//	b if_okay2
//#endif

#endif
