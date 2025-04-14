#include "../equates.h"
#include "../6502mac.h"

MAPPER_OVERLAY_TEXT(8)

	global_func mapper73init
	global_func mapper_73_handler

 counter = mapperdata
 irqen = mapperdata+4
 counter_last_timestamp = mapperdata+8
@----------------------------------------------------------------------------
mapper73init:	@Konami Salamander (J)...
@----------------------------------------------------------------------------
	.word write8000,writeA000,writeC000,writeE000
	bx lr
@-------------------------------------------------------
write8000:
@-------------------------------------------------------
	ldr_ r2,counter
	and r0,r0,#0xF
	tst addy,#0x1000
	bne write9000
	bic r2,r2,#0x000F
	orr r0,r2,r0
	str_ r0,counter
	bx lr
write9000:
	bic r2,r2,#0x00F0
	orr r0,r2,r0,lsl#4
	str_ r0,counter
	bx lr

@-------------------------------------------------------
writeA000:
@-------------------------------------------------------
	ldr_ r2,counter
	and r0,r0,#0xF
	tst addy,#0x1000
	bne writeB000
	bic r2,r2,#0x0F00
	orr r0,r2,r0,lsl#8
	str_ r0,counter
	bx lr
writeB000:
	bic r2,r2,#0xF000
	orr r0,r2,r0,lsl#12
	str_ r0,counter
	bx lr

@-------------------------------------------------------
writeC000:
@-------------------------------------------------------
	@Acknowledge IRQ
	ldrb_ r2,wantirq
	bic r2,r2,#IRQ_MAPPER
	strb_ r2,wantirq
	
	ldrb_ r1,irqen
	tst addy,#0x1000
	bne writeD000
	strb_ r0,irqen
	@r0 = .....MEA  M = mode, E = enable, A = auto enable on D000 write
	
	@if IRQ was disabled, don't run IRQ counter.
	tst r1,#2
	beq 0f
	stmfd sp!,{r0,r1,lr}
	bl run_counter
	ldmfd sp!,{r0,r1,lr}
0:
	@if enable IRQ bit set, set latch to counter
	tst r0,#2
	ldr_ r1,counter
	bic r1,r1,#0xFF000000
	bic r1,r1,#0x00FF0000
	orr r1,r1,r1,lsl#16
	str_ r1,counter
	b find_next_irq
writeD000:
	@if IRQ is enabled, and this operation disables IRQ, run IRQ counter
	tst r1,#2  @is IRQ enabled, jump ahead if not
	beq 0f
	tst r1,#1  @is auto-enable on?
	bxne lr	   @no change in IRQ enable, just return
	stmfd sp!,{r1,lr}
	bl run_counter
	ldmfd sp!,{r1,lr}
	bic r1,r1,#2
	strb_ r1,irqen
	bx lr
0:	
	@if enable IRQ bit is set, enable IRQ
	tst r1,#1
	orrne r1,r1,#2
	strneb_ r1,irqen
	bne find_next_irq
	bx lr

@-------------------------------------------------------
writeE000:
@-------------------------------------------------------
	tst addy,#0x1000
	bne_long map89AB_
	bx lr

@--------------
@mapper IRQs
@--------------
mapper_73_handler:
	ldr_ r2,mapper_timestamp
	bl run_counter_2
	b_long _GO
run_counter:
	@get timestamp
	ldr_ r1,cycles_to_run
	sub r1,r1,cycles,asr#CYC_SHIFT
	ldr_ r2,timestamp
	add r2,r2,r1
run_counter_2:
	@r2 = timestamp to run counter to
	add r2,r2,#12
	ldr_ r1,counter_last_timestamp
	str_ r2,counter_last_timestamp
	sub r1,r2,r1
	@r1 = elapsed cycles
	
	ldr_ r0,irqen
	@are IRQs enabled?
	tst r0,#0x2
	bxeq lr
	
	@todo: 8/16 bit counter mode, supports only 16 bit mode for now (if value & 4, it's 8-bit mode)
	ldr r0,=0x55555556 @1/3
	umull addy,r1,r0,r1
	ldr_ r0,counter		@32 bit number, top 16 bits are counter, low 16 bits are latch
	adds r0,r0,r1,lsl#16
	movccs addy,r1,lsl#16
	addcs r0,r0,r0,lsl#16
	str_ r0,counter
	bxcc lr   @if counter doesn't overflow, we're done
	
	@r2 = timestamp
	
	stmfd sp!,{r2,lr}
	mov r1,r2
	ldr r0,=mapper_irq_handler
	adrl_ r12,mapper_irq_timeout
	@r1 = target timestamp, r0 = handler, r12 = timeout list entry
	bl_long replace_timeout_2
	ldmfd sp!,{r1,lr}
	b find_next_irq_2
	
find_next_irq:
	@get timestamp
	ldr_ r2,cycles_to_run
	sub r2,r2,cycles,asr#CYC_SHIFT
	ldr_ r1,timestamp
	add r1,r1,r2
	@r1 = current timestamp now
	add r1,r1,#12
	str_ r1,counter_last_timestamp
find_next_irq_2:
	@r1 = starting timestamp
	ldrb_ r0,irqen
	tst r0,#0x2   @return if irq disabled
	bxeq lr
	@todo: 8/16 bit counter mode, just 16 bit for now  (if value & 4, it's 8-bit mode)
	ldr_ r0,counter
	mov r0,r0,lsr#16
	rsb r0,r0,#0x10000
	add r0,r0,r0,lsl#1
	add r1,r1,r0
	sub r1,r1,#12
	adr r0,mapper_73_handler
	adrl_ addy,mapper_timeout
	b_long replace_timeout_2

@-------------------------------------------------------
	@.end
