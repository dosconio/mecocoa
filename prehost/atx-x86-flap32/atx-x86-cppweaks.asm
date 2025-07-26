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


%macro PG_PUSH 0
	MOV EAX, CR3
	PUSH EAX
	CMP EAX, 0x00001000
	JE  %%ENDO
	CALL ConvertStackPointer
	MOV EBX, 0x00001000
	MOV CR3, EBX
	MOV ESP, EAX
	%%ENDO:
%endmacro
%macro PG_POP 0
	POP EAX
	MOV CR3, EAX
%endmacro

ConvertStackPointer:; (EAX)->Phyzik(EAX)
	MOV EBX, ESP
	SHR EBX, 22; 22 for L1P_ID
	MOV EAX, [EAX + EBX * 4]
	AND EAX, 0xFFFFF000
	MOV EBX, ESP
	SHR EBX, 12; 12 for L0P_ID
	AND EBX, 0x3FF
	MOV EAX, [EAX + EBX * 4]
	AND EAX, 0xFFFFF000
	MOV EBX, ESP
	AND EBX, 0xFFF
	OR  EAX, EBX
	ADD EAX, 4; Skip Ret-address
	RET
	; no use kernel stack

Handint_PIT_Entry:
	ENTER 0,0
	PUSHAD
	MOV AL, ' '
	OUT 0x20, AL
	PG_PUSH
	CALL Handint_PIT; +0x80000000;{} AASM not-support CALL Label|0x80000000, so this or CALL EAX
	PG_POP
	POPAD
	LEAVE
	IRETD
Handint_RTC_Entry:
	ENTER 0,0
	PUSHAD
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	PG_PUSH
	CALL Handint_RTC
	PG_POP
	POPAD
	LEAVE
	IRETD
Handint_KBD_Entry:
	ENTER 0,0
	PUSHAD
	MOV AL, ' '
	OUT 0x20, AL
	PG_PUSH
	CALL Handint_KBD
	PG_POP
	POPAD
	LEAVE
	IRETD
