; Mecocoa Cortex-M context switch.
; SwitchTaskContext() records nex/crt and pends PendSV, PendSV_Handler performs the swap.
; Thread mode must run on PSP with SPSEL set (boot path) before scheduling starts.
; Offsets follow struct NormalTaskContext of unisym inc/c/task.h.

	AREA	|.text|, CODE, READONLY
	PRESERVE8
	THUMB

SwitchTaskContext	PROC
	EXPORT	SwitchTaskContext
	MOV32	R2, #0xE000ED22	; SHPR3 byte 2: PendSV priority
	MOVS	R3, #0xFF	; 15, the lowest, so the swap waits for the other handlers
	STRB	R3, [R2]
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
	MRS	R0, PSP	; the hardware frame of the outgoing task is on its own PSP
	LDR	R1, =C_SwitchCrt
	LDR	R1, [R1]
	ADD	R2, R1, #0x10
	STMIA	R2, {R4-R11}	; ctx->R4-R11, R0-R3 and R12 stay in the hardware frame
	STR	R0, [R1, #0x34]	; ctx->sp
	STR	LR, [R1, #0x38]	; ctx->exc_return
	MRS	R2, CONTROL
	STR	R2, [R1, #0x48]	; ctx->CONTROL
	TST	LR, #0x10	; bit 4 clear: the hardware frame carries the FP state too
	BEQ	PendSV_SaveFP
PendSV_Saved
	LDR	R0, =C_SwitchNext
	LDR	R0, [R0]
	LDR	LR, [R0, #0x38]	; exc_return of the incoming task
	LDR	R1, [R0, #0x34]	; its stack
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	TST	LR, #0x10
	BEQ	PendSV_RestoreFP
PendSV_Restored
	MSR	PSP, R1
	BX	LR	; exception return pops the frame of the incoming task
PendSV_SaveFP
	ADD	R2, R1, #0xA0
	VSTMIA	R2, {S16-S31}	; S0-S15 are stacked by hardware
	VMRS	R3, FPSCR
	STR	R3, [R1, #0x1A0]
	B	PendSV_Saved
PendSV_RestoreFP
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	B	PendSV_Restored
	ENDP

DirectTaskContext	PROC
	EXPORT	DirectTaskContext
	LDR	LR, [R0, #0x38]
	LDR	R1, [R0, #0x34]
	ADD	R2, R0, #0x10
	LDMIA	R2, {R4-R11}
	TST	LR, #0x10
	BEQ	Direct_RestoreFP
Direct_Restored
	MSR	PSP, R1
	BX	LR
Direct_RestoreFP
	ADD	R2, R0, #0xA0
	VLDMIA	R2, {S16-S31}
	LDR	R3, [R0, #0x1A0]
	VMSR	FPSCR, R3
	B	Direct_Restored
	ENDP

	AREA	|.bss|, DATA, READWRITE, NOINIT, ALIGN=2
C_SwitchNext	SPACE	4
C_SwitchCrt	SPACE	4

	ALIGN
	END