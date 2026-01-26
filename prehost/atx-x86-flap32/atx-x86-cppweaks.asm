; ASCII AASM TAB4 LF
; Attribute: 
; LastCheck: 20240218
; AllAuthor: @ArinaMgk
; ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "osdev.a"
%include "ladder.a"

SegData EQU 8*1

GLOBAL SwitchReal16
GLOBAL SW16_FUNCTION
;
GLOBAL VideoModeVal
GLOBAL SwitchVideoMode
;
GLOBAL MemoryList
GLOBAL MemoryListData

; UNISYM BIOS Real16 and its ld. Then we can call it. (`00080000`~`0009FFFF`)
; Link this below 0x10000

INTTBL_8616: DW 256 * 4 - 1; READONLY
	DD 0
INTTBL_8632: DW 0
	DD 0
SW16_FUNCTION: DW 0

[BITS 32]
SwitchReal16:
	; flap32 -> real16
	PUSHAD
	JMP DWORD 8*4:PointReal16;{} can do this directly
[BITS 16]
	EnterCo16
	CALL [SW16_FUNCTION]
	MOV EAX, CR0
	OR  EAX, 0x80000001
	MOV CR0, EAX
	Addr20Enable
	JMP WORD 8*2:PointBack32
[BITS 32]
PointBack32:
	LoadDataSegs SegData
	POPAD
	LIDT [INTTBL_8632]
RET

[BITS 16]
VideoModeVal: DW 0; IN(mode) OUT(0 for good), should below 0x10000
SwitchVideoMode:
	MOV AX, 0xB800
	MOV ES, AX
	MOV BYTE [ES:0], 'X'
		MOV AX, 0x7800
		MOV ES, AX
		XOR DI, DI; 0x00078000
		; Seek
		MOV AX, 0x4F01; VBE function: Get ModeInfo
		MOV CX, [VideoModeVal]
		INT 0x10
		CMP AX, 0x004F
		JNE SwitchVideoModeFail
		; Set Mode
		XOR AX, AX
		MOV DS, AX
		MOV AX, 0x4F02
		MOV BX, [VideoModeVal]
		OR  BX, 0x4000
		INT 0x10
		MOV WORD[VideoModeVal], 0
	; HLT
	RET
	SwitchVideoModeFail:
		XOR AX, AX
		MOV DS, AX
		MOV WORD[VideoModeVal], 1
		RET

MemoryList:
	XOR EBX, EBX
	MOV EDI, MemoryListData
	_MemoryList_Loop:
		MOV EAX, 0xE820
		MOV ECX, 20
		MOV EDX, 0x534D4150; SMAP
		INT 0x15
		JC  _MemoryList_Fail
		ADD EDI, ECX
		CMP EBX, 0
		JNE _MemoryList_Loop
	_MemoryList_Fail:
	RET

MemoryListData: TIMES 20*5 DD 0


; Link this below or above 0x10000

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
; ---- SLAVE BELOW ----
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

