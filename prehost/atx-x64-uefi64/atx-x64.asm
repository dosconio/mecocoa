
[BITS 64]

EXTERN kernel_stack

EXTERN EnableSSE
EXTERN _ZN6Memory9clear_bssEv
EXTERN _preprocess

EXTERN mecocoa

GLOBAL _entry

SegData EQU 8*1
SegCo32 EQU 8*2

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
	MOV DWORD [RAX+4*3], 0x00CF9200; SegData
	MOV DWORD [RAX+4*5], 0x00CF9A00; SegCo32
	MOV DWORD [RAX+4*9], 0x000F9A00; SegCo16
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


