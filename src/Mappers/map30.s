#include "../equates.h"

MAPPER_OVERLAY_TEXT(5)

	global_func mapper30init
@----------------------------------------------------------------------------
mapper30init:
@----------------------------------------------------------------------------
	.word mapCDEF_,mapCDEF_,mapCDEF_,mapCDEF_
	@todo: >8K CHR-RAM support  (not yet implemented)
	@if >8K CHR-RAM, change to map30w (not yet implemented)
	@if Single Screen mirroring, change to map30w_singlescreen
	ldrb_ r0,singlescreen
	movs r0,r0
	bxeq lr
	ldr r0,=map30w_singlescreen
mapper30_setwrites:
	.if PRG_BANK_SIZE == 8
	str_ r0,writemem_8
	str_ r0,writemem_A
	str_ r0,writemem_C
	str_ r0,writemem_E
	.endif
	.if PRG_BANK_SIZE == 4
	str_ r0,writemem_8
	str_ r0,writemem_9
	str_ r0,writemem_A
	str_ r0,writemem_B
	str_ r0,writemem_C
	str_ r0,writemem_D
	str_ r0,writemem_E
	str_ r0,writemem_F
	.endif
	bx lr
MAPPER_OVERLAY(5)
map30w_singlescreen:
	stmfd sp!,{r0,lr}
	mov r12,r0
	tst r0,#0x80
	bl mirror1_
	mov r0,r12,lsr#5
	b 1f
map30w:
	stmfd sp!,{r0,lr}
	mov r0,r0,lsr#5
1:
	bl chr01234567_
	ldmfd sp!,{r0,lr}
	b mapCDEF_
