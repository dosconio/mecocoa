; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240220
; AllAuthor: @dosconio
; ModuTitle: Handlers for interruptions from hard or soft sources
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "debug.a"

GLOBAL _Handint_General

GLOBAL _Handexc_0_Divide_By_Zero
GLOBAL _Handexc_1_Step
GLOBAL _Handexc_2_NMI
GLOBAL _Handexc_3_Breakpoint
GLOBAL _Handexc_4_Overflow
GLOBAL _Handexc_5_Bound
GLOBAL _Handexc_6_Invalid_Opcode
GLOBAL _Handexc_7_Coprocessor_Not_Available
GLOBAL _Handexc_8_Double_Fault
GLOBAL _Handexc_9_Coprocessor_Segment_Overrun
GLOBAL _Handexc_A_Invalid_TSS
GLOBAL _Handexc_E_Page_Fault
GLOBAL _Handexc_Else

GLOBAL _Handint_CLK
EXTERN _Hand_CLK
;{MORE}

EXTERN _Handexc

%include "mecocoa/kernel.inc"

%macro calli 1-2 0
	PUSHAD
;;	CALL DWORD %1   ;{TODO}
	%ifnidn %2, 0
		ADD ESP, 4*(%2)
	%endif
	POPAD
	IRET
%endmacro

[CPU 386]

[BITS 32]

section .text

; ---- Master PIC Device

_Handint_CLK:; IRQ 0 by 0x20
	calli _Hand_CLK

_Handint_KBD:; IRQ 1 by 0x21
	calli _Hand_KBD

_Handint_CAS:; IRQ 2 by 0x22
	calli _Hand_CAS

_Handint_MOU:; IRQ 3 by 0x23
	calli _Hand_MOU;?
	; Second Serial?

_Handint_COM1:; IRQ 4 by 0x24
	calli _Hand_COM1;?
	; First Serial?

_Handint_COM2:; IRQ 5 by 0x25
	calli _Hand_COM2;?
	; XT Winchester?

_Handint_LPT1:; IRQ 6 by 0x26
	calli _Hand_LPT1;?
	; Floppy?

_Handint_PRT:; IRQ 7 by 0x27
	calli _Hand_PRT; Printer

;

_Handint_IRQ9:
	calli _Hand_IRQ9
	; Redirected?

_Handint_IRQ10:
	calli _Hand_IRQ10

_Handint_IRQ11:
	calli _Hand_IRQ11

_Handint_IRQ12:
	calli _Hand_IRQ12

_Handint_IRQ13:
	calli _Hand_IRQ13
	; FPU exception?

_Handint_IRQ14:
	calli _Hand_IRQ14
	; AT Winchester?

_Handint_IRQ15:
	calli _Hand_IRQ15

; ---- exception handlers ----
; parameter existed: 8, A~E

_Handexc_0_Divide_By_Zero:
	MOV EDX, 0
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_1_Step:
	MOV EDX, 1
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_2_NMI:
	MOV EDX, 2
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_3_Breakpoint:
	MOV EDX, 3
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_4_Overflow:
	MOV EDX, 4
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_5_Bound:
	MOV EDX, 5
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_6_Invalid_Opcode:
	MOV EDX, 6
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_7_Coprocessor_Not_Available:
	MOV EDX, 7
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_8_Double_Fault:
	MOV EDX, 8
	JMP _Handexc_General

_Handexc_9_Coprocessor_Segment_Overrun:
	MOV EDX, 9
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_A_Invalid_TSS:
	MOV EDX, 10
	JMP _Handexc_General

_Handexc_B_Segment_Not_Present:
	MOV EDX, 11
	JMP _Handexc_General

_Handexc_C_Stack_Segment_Fault:
	MOV EDX, 12
	JMP _Handexc_General

_Handexc_D_General_Protection:
	MOV EDX, 13
	JMP _Handexc_General

_Handexc_E_Page_Fault:
	MOV EDX, 14
	JMP _Handexc_General

; none for F(15)

_Handexc_G_x87_FPU_Floating_Point_Error:
	MOV EDX, 16
	NOT EDX; No parameter
	JMP _Handexc_General

_Handexc_Else:
	MOV EDX, 0x20
	; JMP _Handexc_General
_Handexc_General:
	MOV EAX, SegData
	MOV DS, EAX
	PUSH DWORD EDX
	CALL _Handexc
	endall:	DbgStop

_Handint_General:
	PUSH EAX
	MOV AL, 0x20; EOI
	OUT 0xA0, AL; SLAVE
	OUT 0x20, AL; MASTER
	POP EAX
	IRETD

section .data
	msg_general_exception:
		DB "General Exception!",10,13,0

