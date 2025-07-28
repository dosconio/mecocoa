; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

GLOBAL SwitchReal16

;{TODO} UNISYM BIOS Real16 and its ld. Then we can call it. (`00080000`~`0009FFFF`)
;{TODO} Link this below 0x10000

;{UNFINISHED}
SwitchReal16:
	[BITS 32]
	; flap32 -> real16
	PUSHAD
	JMP DWORD 8*4:PointReal16;{} can do this directly
	[BITS 16]
	PointReal16:
		MOV EAX, CR0
		AND EAX, 0x7FFFFFFE
		MOV CR0, EAX
		JMP WORD 0:PointReal16_switch_CS
	PointReal16_switch_CS:
		MOV AX, 0
		MOV SS, AX
		MOV DS, AX
		MOV ES, AX
		MOV FS, AX
		MOV GS, AX

	MOV EAX, CR0
	OR EAX, 0x80000001
	MOV CR0, EAX
	JMP WORD 8*2:PointBack32
	[BITS 32]
	PointBack32:
		MOV EAX, 8*1
		MOV DS, AX
		MOV ES, AX
		MOV FS, AX
		MOV GS, AX
		MOV SS, AX
		POPAD
	RET

[BITS 32]
GLOBAL Handint_PIT_Entry
EXTERN Handint_PIT
GLOBAL Handint_RTC_Entry
EXTERN Handint_RTC
GLOBAL Handint_KBD_Entry
EXTERN Handint_KBD

GLOBAL ConvertStackPointer
GLOBAL PG_PUSH, PG_POP


PG_PUSH:
	POP ESI
	MOV EAX, DS
	PUSH EAX
	MOV EAX, SS
	PUSH EAX
	;
	MOV EAX, 8 * 1
	MOV DS, EAX
	MOV ES, EAX
	MOV FS, EAX
	MOV GS, EAX
	MOV SS, EAX
	;
	MOV EDX, CR3
	MOV ECX, ESP
	SUB ECX, 4 * 3; ecx and edx and ebp
	PUSH EDX
	PUSH ECX
		PUSH EBP
	MOV EAX, 0x00001000
	MOV CR3, EAX
	;MOV ESP, 0x7000; INTERRUPT STACK
	CALL ConvertStackPointer
	MOV ESP, EAX
		MOV ECX, EBP
		CALL ConvertStackPointer
		MOV EBP, EAX
	PUSH ESI
	RET
PG_POP:
	POP ESI
	    POP EBX
	POP ECX
	POP EDX
	ADD ECX, 4 * (2+1)
	MOV CR3, EDX
	MOV ESP, ECX
	POP EAX
	MOV SS, EAX
	POP EAX
	MOV DS, EAX
	MOV ES, EAX
	MOV FS, EAX
	MOV GS, EAX
	PUSH ESI
	RET

ConvertStackPointer:; (ECX:ESP, EDX:CR3)->ESP
	MOV EBX, ECX
	SHR EBX, 22; 22 for L1P_ID
	MOV EAX, [EDX + EBX * 4]
	AND EAX, 0xFFFFF000
	MOV EBX, ECX
	SHR EBX, 12; 12 for L0P_ID
	AND EBX, 0x3FF
	MOV EAX, [EAX + EBX * 4]
	AND EAX, 0xFFFFF000
	MOV EBX, ECX
	AND EBX, 0xFFF
	OR  EAX, EBX
	; ADD EAX, 4; Skip Ret-address
	RET
	; no use kernel stack

Handint_PIT_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_PIT
	CALL PG_POP
	POPAD
	IRETD
Handint_RTC_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	CALL Handint_RTC
	CALL PG_POP
	POPAD
	IRETD
Handint_KBD_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_KBD
	CALL PG_POP
	POPAD
	IRETD

