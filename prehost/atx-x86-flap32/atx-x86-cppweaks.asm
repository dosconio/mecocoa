; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "osdev.a"
%include "ladder.a"

SegData EQU 8*1

[BITS 32]
; 0x20
GLOBAL Handint_PIT_Entry
EXTERN Handint_PIT
; 0x21
GLOBAL Handint_KBD_Entry
EXTERN Handint_KBD
; 0x70
GLOBAL Handint_RTC_Entry
EXTERN Handint_RTC
; 0x74 Mouse
GLOBAL Handint_MOU_Entry
EXTERN Handint_MOU
; 0x76 0x77
GLOBAL Handint_HDD_Entry
EXTERN Handint_HDD

EXTERN PG_PUSH, PG_POP

GLOBAL RefreshGDT
RefreshGDT:
	LoadDataSegs SegData
	MOV EAX, 8*2
	PUSH EAX
	MOV EAX, RefreshGDT_NEXT
	PUSH EAX
RETF
RefreshGDT_NEXT: RET

Handint_PIT_Entry:
	PUSHAD
	CALL PG_PUSH
	MOV AL, ' '
	OUT 0x20, AL
	CALL Handint_PIT
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
; ---- SLAVE PIC BELOW ----
Handint_RTC_Entry:
	PUSHAD
	CALL PG_PUSH
	CALL Handint_RTC
	CALL PG_POP
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	POPAD
	IRETD
Handint_MOU_Entry:
	PUSHAD
	CALL PG_PUSH
	CALL Handint_MOU
	CALL PG_POP
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	POPAD
	IRETD
Handint_HDD_Entry:
	PUSHAD
	CALL PG_PUSH
	CALL Handint_HDD
	CALL PG_POP
	MOV AL, ' '
	OUT 0xA0, AL
	OUT 0x20, AL
	POPAD
	IRETD

