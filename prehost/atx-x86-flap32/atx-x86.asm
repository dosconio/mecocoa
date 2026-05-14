; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "osdev.a"
%include "ladder.a"

SegData EQU 8*3
SegCo32 EQU 8*4

[BITS 32]

GLOBAL Handint_SYSCALL_Entry
EXTERN Handint_SYSCALL
Handint_SYSCALL_Entry:
	PUSHFD
	CLI
	PUSHAD
	CALL PG_PUSH; leave 20 bytes
	LEA EAX, [ESP + 20]
	PUSH EAX
	CALL Handint_SYSCALL
	ADD ESP, 4
	MOV [ESP + 48], EAX;// write back
	CALL PG_POP
	POPAD
	POPFD; STI
RETF

GLOBAL Handint_INTCALL_Entry
Handint_INTCALL_Entry:
	PUSHFD
	CLI
	PUSHAD
	CALL PG_PUSH; leave 20 bytes
	; CallgateFrame* is ESP + 20
	LEA EAX, [ESP + 20]
	PUSH EAX
	CALL Handint_SYSCALL
	ADD ESP, 4
	MOV [ESP + 48], EAX; write back EAX to the saved PUSHAD state
	CALL PG_POP
	POPAD
	POPFD
IRETD

EXTERN interrupt_dispatcher
EXTERN PG_PUSH, PG_POP

GLOBAL Handint_Common_Stub
Handint_Common_Stub:
	PUSHAD
	CALL PG_PUSH; leave 20 bytes
	PUSH ESP ; Pass Context pointer as 2nd argument
	PUSH DWORD [ESP + 56] ; Interrupt ID (original offset 52 + 4)
	CALL interrupt_dispatcher
	ADD ESP, 8 ; Clean up 2 arguments
	CALL PG_POP
	POPAD
	ADD ESP, 4; Pop Interrupt ID
	IRETD

%macro IRQ_TRAMPOLINE 2
GLOBAL %1
%1:
	PUSH %2
	JMP Handint_Common_Stub
%endmacro

; 0x20..0x27
IRQ_TRAMPOLINE Handint_PIT_Entry, 0x20
IRQ_TRAMPOLINE Handint_KBD_Entry, 0x21
IRQ_TRAMPOLINE Handint_CAS_Entry, 0x22
IRQ_TRAMPOLINE Handint_COM2_Entry, 0x23
IRQ_TRAMPOLINE Handint_COM1_Entry, 0x24
IRQ_TRAMPOLINE Handint_LPT2_Entry, 0x25
IRQ_TRAMPOLINE Handint_FLP_Entry, 0x26
IRQ_TRAMPOLINE Handint_LPT1_Entry, 0x27

; 0x70..0x77
IRQ_TRAMPOLINE Handint_RTC_Entry, 0x70
IRQ_TRAMPOLINE Handint_MOU_Entry, 0x74
IRQ_TRAMPOLINE Handint_HDD_Entry, 0x76
IRQ_TRAMPOLINE Handint_HDD1_Entry, 0x77

GLOBAL RefreshGDT
RefreshGDT:
	LoadDataSegs SegData
	MOV EAX, SegCo32
	PUSH EAX
	MOV EAX, RefreshGDT_NEXT
	PUSH EAX
	RETF
RefreshGDT_NEXT: RET

LocalEOI:


