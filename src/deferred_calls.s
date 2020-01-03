#include "equates.h"

#if COMPY

global_func add_deferred_call
global_func run_deferred_calls
global_func set_deferred_call_ptrs

.text
.thumb

.global deferred_call_ptr
.global deferred_call_base

@void add_deferred_call(int arg1, int arg2, int arg3, void *FP)

run_deferred_calls:
	push {r4,lr}
	ldr r4,_deferred_call_base
run_deferred_calls_loop:
	ldmia r4!,{r0,r1,r2,r3}
	mov r3,r3
	beq run_deferred_calls_exit
	bl bx_r3
	b run_deferred_calls_loop
bx_r3:
	bx r3
run_deferred_calls_exit:
	pop {r4,pc}
set_deferred_call_ptrs:
	adr r1,_deferred_call_ptr
	stmia r1!,{r0}
	stmia r1!,{r0}
	bx lr


add_deferred_call:
	push {r4,r5,lr}
	adr r5,_deferred_call_ptr
	ldr r4,[r5]
	stmia r4!,{r0,r1,r2,r3}
	str r4,[r5]
	pop {r4,r5,pc}



deferred_call_ptr:
_deferred_call_ptr:
	.word 0
deferred_call_base:
_deferred_call_base:
	.word 0

#endif
