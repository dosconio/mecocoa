; Mecocoa Cortex-M context switch.
; SwitchTaskContext() records nex/crt and pends PendSV, PendSV_Handler performs the swap.
; Thread mode must run on PSP with SPSEL set (boot path) before scheduling starts.
; Offsets follow struct NormalTaskContext of unisym inc/c/task.h.

	AREA	|.text|, CODE, READONLY
	PRESERVE8
	THUMB

	IMPORT	SysTick_Handler_Mcca
	IMPORT	syscall
	IMPORT	|Image$$ER_RMOD$$Base|
	IMPORT	|Image$$ER_RMOD$$Limit|

; Keep the interrupted task's EXC_RETURN: PendSV tail-chained from this handler sees 0xFFFFFFF1
SysTick_Entry	PROC
	EXPORT	SysTick_Entry
	PUSH	{R0, LR}
	TST	LR, #0x8	; bit3 clear: nested in another handler, keep the last task EXC_RETURN
	BEQ	SysTick_Stashed
	LDR	R0, =C_SysTickLR
	STR	LR, [R0]
SysTick_Stashed
	BL	SysTick_Handler_Mcca
	POP	{R0, LR}
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
; On entry R0-R3 already hold (callid, p1, p2, p3), R12 the caller's LR and LR the interrupted PC.
SvcSyscallTrampoline	PROC
	EXPORT	SvcSyscallTrampoline
	PUSH	{R12, LR}
	BL	syscall
	POP	{R12, R1}
	ORR	R1, R1, #1	; the stacked PC carries no Thumb bit, BX needs it
	MOV	LR, R12
	BX	R1
	ENDP

SwitchTaskContext	PROC
	EXPORT	SwitchTaskContext
	MOV32	R2, #0xE000ED22	; SHPR3 bytes 2 and 3: PendSV and SysTick priority
	MOV32	R3, #0x0000FFFF
	STRH	R3, [R2]	; 15 for both, so SysTick never preempts the switch
	LDR	R2, =C_SwitchNext
	STR	R0, [R2]
	LDR	R2, =C_SwitchCrt
	STR	R1, [R2]
	MOV32	R2, #0xE000ED04	; ICSR
	MOV32	R3, #0x10000000	; PENDSVSET
	STR	R3, [R2]
	DSB
	ISB
	BX	LR
	ENDP

PendSV_Handler	PROC
	EXPORT	PendSV_Handler
	TST	LR, #0x8	; bit3 clear: entered from a handler, so LR is not the task's
	BEQ	PendSV_FromHandler
	B	PendSV_LRReady
PendSV_FromHandler
	LDR	R3, =C_SysTickLR
	LDR	LR, [R3]	; EXC_RETURN of the task being switched out
PendSV_LRReady
	MRS	R0, PSP	; the hardware frame of the outgoing task is on its own PSP
	LDR	R1, =C_SwitchCrt
	LDR	R1, [R1]
	ADD	R2, R1, #0x10
	STMIA	R2, {R4-R11}	; ctx->R4-R11, R0-R3 and R12 stay in the hardware frame
	STR	R0, [R1, #0x34]	; ctx->sp
	STR	LR, [R1, #0x38]	; ctx->exc_return (bit4 tells whether the frame carries FP)
	MRS	R0, MSP
	STR	R0, [R1, #0x4C]	; ctx->SP_svc: this task's handler stack
	MRS	R2, CONTROL
	AND	R2, R2, #0x1	; ctx->CONTROL: nPRIV only, SPSEL and FPCA belong to the exception entry/return
	STR	R2, [R1, #0x48]
	ADD	R2, R1, #0xA0
	VSTMIA	R2, {S16-S31}	; S0-S15 are stacked by hardware, S16-S31 never are
	VMRS	R3, FPSCR
	STR	R3, [R1, #0x1A0]
	MOVS	R2, #0
	STR	R2, [R1, #0x1BC]	; ThreadBlock::just_schedule: context saved, may be picked again
	;
	LDR	R0, =C_SwitchNext
	LDR	R0, [R0]
	LDR	LR, [R0, #0x38]	; exc_return of the incoming task
	LDR	R1, [R0, #0x34]	; its stack
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	MSR	PSP, R1
	LDR	R1, [R0, #0x4C]	; this task's handler stack
	MSR	MSP, R1
	LDR	R3, [R0, #0x48]	; ctx->CONTROL: apply nPRIV, the stack and FPCA follow EXC_RETURN
	AND	R3, R3, #0x1
	MRS	R2, CONTROL
	BIC	R2, R2, #0x1
	ORR	R3, R3, R2
	MSR	CONTROL, R3
	ISB
	BX	LR	; exception return pops the frame of the incoming task
	ENDP

DirectTaskContext	PROC
	EXPORT	DirectTaskContext
	LDR	LR, [R0, #0x38]
	LDR	R1, [R0, #0x34]
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	MSR	PSP, R1
	LDR	R3, [R0, #0x48]	; ctx->CONTROL: apply nPRIV only
	AND	R3, R3, #0x1
	MRS	R2, CONTROL
	BIC	R2, R2, #0x1
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

	ALIGN
	END
