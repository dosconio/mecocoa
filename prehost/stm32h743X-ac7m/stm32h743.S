; Mecocoa Cortex-M context switch.
; SwitchTaskContext() records nex/crt and pends PendSV, PendSV_Handler performs the swap.
; Thread mode must run on PSP with SPSEL set (boot path) before scheduling starts.
; Offsets follow struct NormalTaskContext of unisym inc/c/task.h.

	AREA	|.text|, CODE, READONLY
	PRESERVE8
	THUMB

	IMPORT	SysTick_Handler_Mcca
	IMPORT	syscall
	IMPORT	interrupt_dispatcher
	IMPORT	C_SvcReturn
	IMPORT	|Image$$ER_RMOD$$Base|
	IMPORT	|Image$$ER_RMOD$$Limit|

; Keep the interrupted task's EXC_RETURN: PendSV tail-chained from this handler sees 0xFFFFFFF1
; A tick that does not end in a switch still runs kernel code, so a thread with a live FP bank
; (FPCA=1) has S0-S15 put on its ctx here and taken back before the interrupted code resumes.
SysTick_Entry	PROC
	EXPORT	SysTick_Entry
	PUSH	{R4, R5, R6, LR}
	MOV	R4, #0	; 0: nothing was saved
	TST	LR, #0x8	; bit3 clear: nested in another handler, keep the last task EXC_RETURN
	BEQ	SysTick_Stashed
	LDR	R6, =C_SysTickLR
	STR	LR, [R6]
SysTick_Stashed
	BL	SysTick_Handler_Mcca
SysTick_Done
	POP	{R4, R5, R6, LR}
	BX	LR
	ENDP

; Bounds of the ER_RMOD region of mcca.sct, for the driver self-registration walk
RMOD_BoundBase	PROC
	EXPORT	RMOD_BoundBase
	LDR	R0, =|Image$$ER_RMOD$$Base|
	BX	LR
	ENDP

RMOD_BoundLimit	PROC
	EXPORT	RMOD_BoundLimit
	LDR	R0, =|Image$$ER_RMOD$$Limit|
	BX	LR
	ENDP

; Run the syscall body in thread mode on the caller's stack, so it may block.
; On entry R0-R3 hold (callid, p1, p2, p3). The handler kept the caller frame on the thread,
; so the body may use this memory as its stack and the return goes through that saved frame.
SvcSyscallTrampoline	PROC
	EXPORT	SvcSyscallTrampoline
	BL	syscall
	MOV	R1, R0	; the result rides back in the marker call
	MVN	R0, #0	; ask the handler to resume the caller from its frame
	SVC	#0
	ENDP

; The SVC vector: the dispatcher picks the EXC_RETURN to use, because a syscall returns
; through the caller frame rather than through this handler's own frame.
SVC_Handler	PROC
	EXPORT	SVC_Handler
	PUSH	{R4, LR}
	MOV	R0, LR
	MRS	R1, IPSR
	BL	interrupt_dispatcher
	POP	{R4, LR}
	LDR	R1, =C_SvcReturn	; nonzero on a syscall return: rewind the kernel stack
	LDR	R2, [R1]
	MOV	R3, #0
	STR	R3, [R1]	; one shot
	CMP	R2, #0
	BEQ	SVC_NoRewind
	MSR	MSP, R2
	CPSIE	i	; the syscall return kept interrupts masked until the frame was rebuilt
SVC_NoRewind
	MOV	LR, R0
	BX	LR
	ENDP

SwitchTaskContext	PROC
	EXPORT	SwitchTaskContext
	MOV32	R2, #0xE000ED22	; SHPR3 bytes 2 and 3: PendSV and SysTick priority
	MOV32	R3, #0x00000408	; SysTick 4, PendSV 8
	STRH	R3, [R2]	; SVCall stays at 15, see syscall.cpp
	LDR	R2, =C_SwitchNext
	STR	R0, [R2]
	LDR	R2, =C_SwitchCrt
	STR	R1, [R2]
	LDR	R2, =C_CurrentCtx
	STR	R1, [R2]	; the thread that keeps running until PendSV really swaps
	MOV32	R2, #0xE000ED04	; ICSR
	MOV32	R3, #0x10000000	; PENDSVSET
	STR	R3, [R2]
	DSB
	ISB
	BX	LR
	ENDP

PendSV_Handler	PROC
	EXPORT	PendSV_Handler
	MRS	R0, PSP	; the thread frame of the outgoing task, wherever it was interrupted
	; With ASPEN off the core never stacks S0-S15, so every thread frame is the basic 8 words on the PSP and the EXC_RETURN is always the thread-mode one.
	MOV32	LR, #0xFFFFFFFD
PendSV_LRReady
	LDR	R1, =C_SwitchCrt
	LDR	R1, [R1]
	ADD	R2, R1, #0x10
	STMIA	R2, {R4-R11}	; ctx->R4-R11, R0-R3 and R12 stay in the hardware frame
	STR	R0, [R1, #0x34]	; ctx->sp
	STR	LR, [R1, #0x38]	; ctx->exc_return
	MRS	R0, MSP
	STR	R0, [R1, #0x4C]	; ctx->SP_svc: this task's handler stack
	MRS	R2, CONTROL
	AND	R2, R2, #0x5	; ctx->CONTROL: nPRIV plus FPCA
	STR	R2, [R1, #0x48]
	TST	R2, #4	; FPCA: only a thread that ran FP has live S0-S31
	BEQ	PendSV_FPSkipSav
	ADD	R2, R1, #0xA0
	VSTMIA	R2, {S16-S31}
	ADD	R2, R1, #0xE0
	VSTMIA	R2, {S0-S15}
	VMRS	R3, FPSCR
	STR	R3, [R1, #0x1A0]
PendSV_FPSkipSav
	MOVS	R2, #0
	STR	R2, [R1, #0x1BC]	; ThreadBlock::just_schedule: context saved, may be picked again
	;
	LDR	R0, =C_SwitchNext
	LDR	R0, [R0]
	LDR	LR, [R0, #0x38]	; exc_return of the incoming task
	LDR	R1, [R0, #0x34]	; its stack
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	LDR	R3, [R0, #0x48]	; ctx->CONTROL: nPRIV plus FPCA
	TST	R3, #4	; restore FP only if this thread had FP CA before it was swapped out
	BEQ	PendSV_FPSkipRes
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	ADD	R2, R0, #0xE0
	VLDMIA	R2, {S0-S15}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	LDR	R3, [R0, #0x48]
PendSV_FPSkipRes
	AND	R3, R3, #0x5
	LDR	R2, [R0, #0x34]	; PSP of the incoming task
	LDR	R1, [R0, #0x4C]	; this task's handler stack
	MSR	PSP, R2
	MSR	MSP, R1
	MRS	R2, CONTROL
	BIC	R2, R2, #0x5
	ORR	R3, R3, R2
	MSR	CONTROL, R3
	ISB
	LDR	R2, =C_CurrentCtx
	STR	R0, [R2]	; the thread that is running now
	BX	LR	; exception return pops the frame of the incoming task
	ENDP

DirectTaskContext	PROC
	EXPORT	DirectTaskContext
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
DirectNoFP
	MSR	PSP, R1
	AND	R3, R3, #0x1	; apply nPRIV only
	MRS	R2, CONTROL
	BIC	R2, R2, #0x5
	ORR	R3, R3, R2
	MSR	CONTROL, R3
	ISB
	BX	LR
	ENDP

	ALIGN
	AREA	|.bss|, DATA, READWRITE, NOINIT, ALIGN=2
C_SwitchNext	SPACE	4
C_SwitchCrt	SPACE	4
C_SysTickLR	SPACE	4
C_CurrentCtx	SPACE	4

	ALIGN
	END
