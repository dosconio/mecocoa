
[BITS 64]

EXTERN kernel_stack
EXTERN init
EXTERN mecocoa
GLOBAL _entry

_entry:
    MOV  RSP, kernel_stack + 1024*1024
	CALL init
	CALL mecocoa
	LUP: HLT
	JMP  LUP

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

global setDSAll  ; void setDSAll(uint16 value);
setDSAll:
	MOV DS, DI
	MOV ES, DI
	MOV FS, DI
	MOV GS, DI
	RET

