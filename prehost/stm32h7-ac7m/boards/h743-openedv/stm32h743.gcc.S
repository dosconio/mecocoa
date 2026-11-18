// ASCII GAS TAB4 LF
// Mecocoa STM32H743 context switching for GNU GCC/GAS, not Keil ARMASM.
// Instruction flow and context offsets follow stm32h743.S.

	.syntax unified
	.cpu cortex-m7
	.fpu fpv5-d16
	.thumb
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1

	.section .text, "ax", %progbits
// Mecocoa Cortex-M context switch.
// SwitchTaskContext() records nex/crt and pends PendSV, PendSV_Handler performs the swap.
// Thread mode must run on PSP with SPSEL set (boot path) before scheduling starts.
// Offsets follow struct NormalTaskContext of unisym inc/c/task.h.


	.extern SysTick_Handler_Mcca
	.extern syscall
	.extern interrupt_dispatcher
	.extern C_SvcReturn
	.extern __init_rmod_ento
	.extern __init_rmod_endo

// Keep the interrupted task's EXC_RETURN: PendSV tail-chained from this handler sees 0xFFFFFFF1
// A tick that does not end in a switch still runs kernel code, so a thread with a live FP bank
// (FPCA=1) has S0-S15 put on its ctx here and taken back before the interrupted code resumes.

	.global SysTick_Entry
	.type SysTick_Entry, %function
	.thumb_func
SysTick_Entry:
	PUSH	{R4, R5, R6, LR}
	MOV	R4, #0 // 0: nothing was saved
	TST	LR, #0x8 // bit3 clear: nested in another handler, keep the last task EXC_RETURN
	BEQ	SysTick_Stashed
	LDR	R6, =C_SysTickLR
	STR	LR, [R6]
SysTick_Stashed:
	BL	SysTick_Handler_Mcca
SysTick_Done:
	POP	{R4, R5, R6, LR}
	BX	LR
	.size SysTick_Entry, .-SysTick_Entry

// Bounds of .init.rmod in mcca.ld for the existing driver self-registration walk

	.global RMOD_BoundBase
	.type RMOD_BoundBase, %function
	.thumb_func
RMOD_BoundBase:
	LDR	R0, =__init_rmod_ento
	BX	LR
	.size RMOD_BoundBase, .-RMOD_BoundBase


	.global RMOD_BoundLimit
	.type RMOD_BoundLimit, %function
	.thumb_func
RMOD_BoundLimit:
	LDR	R0, =__init_rmod_endo
	BX	LR
	.size RMOD_BoundLimit, .-RMOD_BoundLimit
	.balign 4
	.ltorg

// Run the syscall body in thread mode on the caller's stack, so it may block.
// On entry R0-R3 hold (callid, p1, p2, p3). The handler kept the caller frame on the thread,
// so the body may use this memory as its stack and the return goes through that saved frame.

	.global SvcSyscallTrampoline
	.type SvcSyscallTrampoline, %function
	.thumb_func
SvcSyscallTrampoline:
	BL	syscall
	MOV	R1, R0 // the result rides back in the marker call
	MVN	R0, #0 // ask the handler to resume the caller from its frame
	SVC	#0
	.size SvcSyscallTrampoline, .-SvcSyscallTrampoline

// The SVC vector: the dispatcher picks the EXC_RETURN to use, because a syscall returns
// through the caller frame rather than through this handler's own frame.

	.global SVC_Handler
	.type SVC_Handler, %function
	.thumb_func
SVC_Handler:
	PUSH	{R4, LR}
	MOV	R0, LR
	MRS	R1, IPSR
	BL	interrupt_dispatcher
	POP	{R4, LR}
	LDR	R1, =C_SvcReturn // nonzero on a syscall return: rewind the kernel stack
	LDR	R2, [R1]
	MOV	R3, #0
	STR	R3, [R1] // one shot
	CMP	R2, #0
	BEQ	SVC_NoRewind
	MSR	MSP, R2
	CPSIE	i // the syscall return kept interrupts masked until the frame was rebuilt
SVC_NoRewind:
	MOV	LR, R0
	BX	LR
	.size SVC_Handler, .-SVC_Handler


	.global SwitchTaskContext
	.type SwitchTaskContext, %function
	.thumb_func
SwitchTaskContext:
	MRS	R12, PRIMASK // the pair below and the PendSV trigger must be seen as one update
	CPSID	i
	movw	r2, #0xED22 // SHPR3 bytes 2 and 3: PendSV and SysTick priority
	movt	r2, #0xE000
	movw	r3, #0x40F0 // SysTick 4, PendSV 15: the swap waits for a thread boundary
	movt	r3, #0x0000
	STRH	R3, [R2] // SVCall stays at 15, see syscall.cpp
	LDR	R2, =C_SwitchNext
	STR	R0, [R2]
	LDR	R2, =C_SwitchCrt
	STR	R1, [R2]
	LDR	R2, =C_CurrentCtx
	STR	R1, [R2] // the thread that keeps running until PendSV really swaps
	movw	r2, #0xED04 // ICSR
	movt	r2, #0xE000
	movw	r3, #0x0000 // PENDSVSET
	movt	r3, #0x1000
	STR	R3, [R2]
	MSR	PRIMASK, R12 // PendSV is taken here, on a complete pair
	DSB
	ISB
	BX	LR
	.size SwitchTaskContext, .-SwitchTaskContext
	.balign 4
	.ltorg


	.global PendSV_Handler
	.type PendSV_Handler, %function
	.thumb_func
PendSV_Handler:
	MRS	R0, PSP // the thread frame of the outgoing task, wherever it was interrupted
// Boot disables ASPEN and clears FPCA, so every thread frame is the basic 8 words on the PSP.
	movw	lr, #0xFFFD
	movt	lr, #0xFFFF
PendSV_LRReady:
	LDR	R1, =C_SwitchCrt
	LDR	R1, [R1]
	ADD	R2, R1, #0x10
	STMIA	R2, {R4-R11} // ctx->R4-R11, R0-R3 and R12 stay in the hardware frame
	STR	R0, [R1, #0x34] // ctx->sp
	STR	LR, [R1, #0x38] // ctx->exc_return
	MRS	R0, MSP
	STR	R0, [R1, #0x4C] // ctx->SP_svc: this task's handler stack
	MRS	R2, CONTROL
	AND	R2, R2, #0x5 // ctx->CONTROL: nPRIV plus FPCA
	STR	R2, [R1, #0x48]
	TST	R2, #4 // FPCA: only a thread that ran FP has live S0-S31
	BEQ	PendSV_FPSkipSav
	ADD	R2, R1, #0xA0
	VSTMIA	R2, {S16-S31}
	ADD	R2, R1, #0xE0
	VSTMIA	R2, {S0-S15}
	VMRS	R3, FPSCR
	STR	R3, [R1, #0x1A0]
PendSV_FPSkipSav:
	MOVS	R2, #0
	STR	R2, [R1, #0x1BC] // ThreadBlock::just_schedule: context saved, may be picked again
//
	LDR	R0, =C_SwitchNext
	LDR	R0, [R0]
	LDR	LR, [R0, #0x38] // exc_return of the incoming task
	LDR	R1, [R0, #0x34] // its stack
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	LDR	R3, [R0, #0x48] // ctx->CONTROL: nPRIV plus FPCA
	TST	R3, #4 // restore FP only if this thread had FP CA before it was swapped out
	BEQ	PendSV_FPSkipRes
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	ADD	R2, R0, #0xE0
	VLDMIA	R2, {S0-S15}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	LDR	R3, [R0, #0x48]
PendSV_FPSkipRes:
	AND	R3, R3, #0x5
	LDR	R2, [R0, #0x34] // PSP of the incoming task
	LDR	R1, [R0, #0x4C] // this task's handler stack
	MSR	PSP, R2
	MSR	MSP, R1
	MRS	R2, CONTROL
	BIC	R2, R2, #0x5
	ORR	R3, R3, R2
	MSR	CONTROL, R3
	ISB
	LDR	R2, =C_CurrentCtx
	STR	R0, [R2] // the thread that is running now
	BX	LR // exception return pops the frame of the incoming task
	.size PendSV_Handler, .-PendSV_Handler
	.balign 4
	.ltorg


	.global DirectTaskContext
	.type DirectTaskContext, %function
	.thumb_func
DirectTaskContext:
	LDR	LR, [R0, #0x38]
	LDR	R1, [R0, #0x34]
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	LDR	R3, [R0, #0x48]
	TST	R3, #4
	BEQ	DirectNoFP
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	ADD	R2, R0, #0xE0
	VLDMIA	R2, {S0-S15}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	LDR	R3, [R0, #0x48]
DirectNoFP:
	MSR	PSP, R1
	AND	R3, R3, #0x1 // apply nPRIV only
	MRS	R2, CONTROL
	BIC	R2, R2, #0x5
	ORR	R3, R3, R2
	MSR	CONTROL, R3
	ISB
	BX	LR
	.size DirectTaskContext, .-DirectTaskContext

	.balign 4
	.section .bss.h7_context, "aw", %nobits
	.type C_SwitchNext, %object
C_SwitchNext:
	.space 4
	.size C_SwitchNext, 4
	.type C_SwitchCrt, %object
C_SwitchCrt:
	.space 4
	.size C_SwitchCrt, 4
	.type C_SysTickLR, %object
C_SysTickLR:
	.space 4
	.size C_SysTickLR, 4
	.type C_CurrentCtx, %object
C_CurrentCtx:
	.space 4
	.size C_CurrentCtx, 4

	.balign 4

	.section .note.GNU-stack, "", %progbits
