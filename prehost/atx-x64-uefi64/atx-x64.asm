
[BITS 64]

EXTERN kernel_stack

EXTERN EnableSSE
EXTERN _ZN6Memory9clear_bssEv
EXTERN _preprocess

EXTERN mecocoa
EXTERN PG_PUSH, PG_POP

GLOBAL _entry

SegCo16 EQU 8*1
SegData EQU 8*3
SegCo32 EQU 8*4

%ifdef _UEFI
_entry:
    MOV  RSP, kernel_stack + 1024*1024
	PUSH RDI
	CALL EnableSSE
	CALL _ZN6Memory9clear_bssEv; Memory::clear_bss
	CALL _preprocess
	POP  RDI
	CALL mecocoa
	LUP: HLT
	JMP  LUP
%else
_entry:
	MOV  RSP, 0x7FF0
	CALL EnableSSE
	CALL _ZN6Memory9clear_bssEv; Memory::clear_bss
	CALL _preprocess
	CALL mecocoa
	LUP: HLT
	JMP  LUP
GLOBAL CallCo16
CallCo16:
	MOV WORD[0x500], DI
	SGDT [GDT_PTR]
	MOV  RAX, [GDT_PTR+2]
	MOV DWORD [RAX+SegData+4], 0x00CF9200; SegData
	MOV DWORD [RAX+SegCo16+4], 0x000F9A00; SegCo16
	MOV DWORD [RAX+SegCo32+4], 0x00CF9A00; SegCo32
	AND RAX, 0x3FFF_FFFF
	MOV [GDT_PTR+2], RAX
	LGDT [GDT_PTR]
	PUSH RCX
	PUSH RDX
	PUSH RBX
	PUSH RBP
	PUSH RSI
	PUSH RDI
	PUSH QWORD BackCo16
	; JUMP TO IA32-COMPATIBLE MODE
	PUSH SegCo32
	PUSH 0x8000
	O64 RETF; return to caller
BackCo16:
	MOV  RBX, [GDT_PTR+2]
	OR   RBX, 0xFFFFFFFF_C0000000
	MOV  [GDT_PTR+2], RBX
	LGDT [GDT_PTR]
	POP  RDI
	POP  RSI
	POP  RBP
	POP  RBX
	POP  RDX
	POP  RCX
	RET
HLT
GDT_PTR:
	DW 0
	DQ 0
%endif

GLOBAL setCSSS  ; void setCSSS(uint16 cs, uint16 ss);
setCSSS:
	PUSH RBP
	MOV  RBP, RSP
	MOV  SS, SI
	MOV  RAX, SetCSSS_NEXT
	PUSH RDI    ; CS
	PUSH RAX    ; RIP
	O64 RETF
	SetCSSS_NEXT:
		MOV RSP, RBP
		POP RBP
RET

GLOBAL setDSAll  ; void setDSAll(uint16 value);
setDSAll:
	MOV DS, DI
	MOV ES, DI
	MOV FS, DI
	MOV GS, DI
	RET

GLOBAL tryUD:
tryUD:
	UD2
	RET

%macro PUSHA64 0
	PUSH R15
	PUSH R14
	PUSH R13
	PUSH R12
	PUSH R11
	PUSH R10
	PUSH R9
	PUSH R8
	PUSH RDI
	PUSH RSI
	PUSH RBP
	; PUSH RSP
	PUSH RBX
	PUSH RDX
	PUSH RCX
	PUSH RAX
%endmacro
%macro POPA64 0
	POP  RAX
	POP  RCX
	POP  RDX
	POP  RBX
	; POP RSP
	POP  RBP
	POP  RSI
	POP  RDI
	POP  R8
	POP  R9
	POP  R10
	POP  R11
	POP  R12
	POP  R13
	POP  R14
	POP  R15
%endmacro

GLOBAL Handint_SYSCALL_Entry
EXTERN SYSCALL_TABLE
Handint_SYSCALL_Entry:
	PUSHA64
	; PUSH RBP
	; PUSH RCX  ; original RIP
	; PUSH R11  ; original RFLAGS, will enable rupt
	MOV RCX, R10
	MOV RBP, RSP
	AND RSP, 0xFFFFFFFFFFFFFFF0
	AND EAX, 0x7FFFFFFF

	PUSH RDI
	PUSH RSI
	PUSH RDX
	PUSH R10
	PUSH R8
	PUSH R9
	PUSH RAX
	CALL PG_PUSH
		POP RCX
		POP R11
	POP  RAX
	POP  R9
	POP  R8
	POP  R10
	POP  RDX
	POP  RSI
	POP  RDI
	PUSH R11
	PUSH RCX
	STI
	CALL [SYSCALL_TABLE + 8 * EAX]; -> RAX; rbx, r12-r15: callee-saved
	CLI
	CALL PG_POP

	MOV RSP, RBP
	; POP R11
	; POP RCX
	; POP RBP
	POPA64
O64 SYSRET


GLOBAL Handint_XHCI_Entry
EXTERN Handint_XHCI
Handint_XHCI_Entry:
	PUSHA64
	CALL PG_PUSH
	CALL Handint_XHCI
	CALL PG_POP
	POPA64
IRETQ

GLOBAL Handint_LAPICT_Entry
EXTERN Handint_LAPICT
Handint_LAPICT_Entry:
	PUSHA64
	CALL PG_PUSH
	CALL Handint_LAPICT
	CALL PG_POP
	POPA64
IRETQ
