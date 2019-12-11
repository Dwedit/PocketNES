#include "../equates.h"
MAPPER_OVERLAY_TEXT(5)

	global_func mapper99init
@----------------------------------------------------------------------------
mapper99init:
@----------------------------------------------------------------------------
	.word empty_W,empty_W,empty_W,empty_W

	ldrb_ r0,cartflags
	orr r0,r0,#VS
	strb_ r0,cartflags

	ldr r0,=write4016
	ldr r1,=joypad_write_ptr
	str r0,[r1]

	mov pc,lr
@----------------------------------------------------------------------------
@ MAPPER_OVERLAY(5)
@----------------------------------------------------------------------------
write4016:
@----------------------------------------------------------------------------
	stmfd sp!,{r0,lr}

	mov r0,r0,lsr#2
	bl_long chr01234567_

	ldmfd sp!,{r0,lr}
	b_long joy0_W
@----------------------------------
	@.end
