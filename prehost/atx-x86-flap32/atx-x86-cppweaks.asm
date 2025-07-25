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

Handint_PIT_Entry:
	PUSH EBP
	MOV EBP, ESP
	PUSHAD
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_PIT
	POPAD
	LEAVE
	IRETD
Handint_RTC_Entry:
	PUSH EBP
	MOV EBP, ESP
	PUSHAD
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	CALL Handint_RTC
	POPAD
	LEAVE
	IRETD
Handint_KBD_Entry:
	PUSH EBP
	MOV EBP, ESP
	;ENTER 0,0
	PUSHAD
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_KBD
	POPAD
	LEAVE
	IRETD
