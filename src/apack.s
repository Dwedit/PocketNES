#include "equates.h"

@	.include "equates.h"

	.if APACK

@APlib ARM7 decompressor by Dan Weiss, based on the original C version
@Takes in raw apacked data, NOT data created by the 'safe' compressor.

 .align
 .pool
 .text
 .align
 .pool

 src .req r0
 dest .req r1
 byte .req r2
 mask .req r3
 gamma .req r4
 lwm .req r6
 recentoff .req r7
 temp .req r8

 global_func depack

@r0 = src
@r1 = dest
@r2 = byte
@r3 = rotating bit mask
@r4 = increasing gamma
@r6 = lwm
@r7 = recentoff
@r8 = lr copy/scratch

	.macro GETBIT @3 instructions
	movs mask,mask,ror #1
	ldrcsb byte,[src],#1
	tst byte,mask
	.endm

	.macro GETBITGAMMA @5 instructions
	mov gamma,gamma,lsl #1
	GETBIT
	addne gamma,gamma,#1
	.endm

depack:
	stmfd sp!,{r4-r10,lr}
	ldrb temp,[src],#1
	strb temp,[dest],#1
	ldr mask,=0x01010101
	b_long aploop_nolwm


 .align
 .pool
 .section .iwram, "ax", %progbits
 .subsection 5
 .align
 .pool
	@depack enters here
aploop_nolwm:
	mov lwm,#0
aploop:
	GETBIT
	bne apbranch1
	@bits: 0
	@single literal byte
	@clears LWM
	ldrb temp,[src],#1
	strb temp,[dest],#1
	b aploop_nolwm
apbranch1:
	GETBIT
	beq apbranch2
	GETBIT
	beq apbranch3
	@bits: xxxx111
	@4-bit offset, if offset is zero, use literal zero instead
	@does not affect recent offset, clears LWM
	mov gamma,#0
	GETBIT
	addne gamma,gamma,#1
	GETBITGAMMA
	GETBITGAMMA
	GETBITGAMMA
	cmp gamma,#0
	ldrneb gamma,[dest,-gamma]
	strb gamma,[dest],#1
	b aploop_nolwm
apbranch3:
	@bits: 011
	@byte = [src++]
	@recent offset = byte >> 1
	@length = 2 + (byte & 1)
	@If offset is zero, it's end of file.
	@set LWM
	ldrb gamma,[src],#1
	movs recentoff,gamma,lsr #1
	beq done
	ldrcsb temp,[dest,-recentoff]
	strcsb temp,[dest],#1
	ldrb temp,[dest,-recentoff]
	strb temp,[dest],#1
	ldrb temp,[dest,-recentoff]
	strb temp,[dest],#1
	mov lwm,#1
	b aploop
apbranch2:
	@Not LWM, bits: <gamma2><gamma1>01, gamma1 > 2: subtract additional 1 from gamma1, otherwise same as LWM
	@LWM, bits: <gamma2><gamma1>01
	@  recent offset = <gamma1 - 2> * 256 + [src++]
	@  length = <gamma2>
	@  if recent offset >= 32000, length++
	@  if recent offset >= 1280, length++
	@  if recent offset < 128, length+=2
	@  set LWM
	@Not LWM, bits: <gamma2>0001  (gamma1 == 2, meaning two zero bits)
	@  offset = previous recent offset
	@  length = gamma2
	@  set LWM
	bl ap_getgamma
	sub gamma,gamma,#2
	cmp lwm,#0
	bne ap_is_lwm
	mov lwm,#1
	cmp gamma,#0
	bne ap_not_zero_gamma

	@This point is Not LWM, value of gamma1 is 2, (bits: <gamma2>0001)
	@Use old recent offset, read gamma2 for length
	bl ap_getgamma
copyloop1:
	ldrb temp,[dest,-recentoff]
	strb temp,[dest],#1
	subs gamma,gamma,#1
	bne copyloop1
	b aploop
	
ap_not_zero_gamma:
	@This point is Not LWM, Value of gamma1 > 2, (bits: <gamma2><gamma1>01)
	@we subtract an additional 1 from gamma1, and proceed to LWM case
	sub gamma,gamma,#1
ap_is_lwm:
	@This point is LWM, (bits: <gamma2><gamma1>01)
	ldrb temp,[src],#1
	add recentoff,temp,gamma,lsl #8
	bl ap_getgamma
	@length = gamma2
	cmp recentoff,#32000
	addge gamma,gamma,#1
	cmp recentoff,#1280
	addge gamma,gamma,#1
	cmp recentoff,#128
	addlt gamma,gamma,#2
copyloop2:
	ldrb temp,[dest,-recentoff]
	strb temp,[dest],#1
	subs gamma,gamma,#1
	bne copyloop2
	b aploop

ap_getgamma:
	mov gamma,#1
ap_getgammaloop:
	GETBITGAMMA
	GETBIT
	bne ap_getgammaloop
	bx lr

done:
	ldmfd sp!,{r4-r10,lr}
	bx lr

	.endif
 @.end

.unreq src
.unreq dest
.unreq byte
.unreq mask
.unreq gamma
.unreq lwm
.unreq recentoff
.unreq temp

