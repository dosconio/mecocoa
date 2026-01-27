; ASCII
; Format: Binary

%include "osdev.a"
%include "ladder.a"

ORG 0x8000
SegData EQU 8*1
SegCo32 EQU 8*2
SegCo16 EQU 8*4
SegCo64 EQU 8*5

ADDR_PARA0 EQU 0x500
ADDR_PARA1 EQU 0x502


[BITS 32]
	PUSHAD
; ia32e -> ia32
	; reset PG. Hardware may auto-reset EFER.LMA
	MOV EAX, CR0
	AND EAX, 0x7FFFFFFF
	MOV CR0, EAX
	; reset EFER.LMA
	MOV ECX, 0xC0000080
	RDMSR
	BTR EAX, 8
	WRMSR
	; PAE
	;	MOV EAX, CR4
	;	BTR EAX, 5; CR4.PAE
	;	MOV CR4, EAX
	; reload segment
	LoadDataSegs SegData
; ia32 -> real
	JMP DWORD SegCo16:PointReal16
[BITS 16]
	EnterCo16
	MOV AX, [ADDR_PARA0]
	CMP AX, FUNC_CNTS
	JAE B16_NEXT
	SHL AX, 1
	ADD AX, FUNC_TABLE
	XCHG BX, AX
	MOV AX, [BX]
	CALL AX
B16_NEXT:
	MOV EAX, CR0
	BTS EAX, 0
	MOV CR0, EAX
	Addr20Enable
	JMP WORD SegCo32:PointBack32
[BITS 32]
PointBack32:
	LoadDataSegs SegData
	LIDT [INTTBL_8632]
	;	MOV EAX, CR4
	;	BTS EAX, 5; CR4.PAE
	;	MOV CR4, EAX
	; Set IA32 EFER.LME
	MOV ECX, 0xC0000080
	RDMSR
	BTS EAX, 8
	WRMSR
	; Open Paging
	MOV EAX, CR0
	BTS EAX, 31; PAGE
	MOV CR0, EAX
	POPAD
	POP EAX
	MOV [KernelJump], EAX
	POP EAX
	MOVZX EAX, WORD[ADDR_PARA0]
JMP FAR [KernelJump]


FUNC_CNTS EQU 0x2
; FUNCTIONS
; 0x00 MemoryList(->addr)
; 0x01 SwitchVideoMode(mode->addr)
; 0x02 VideoModeList()
FUNC_TABLE:
DW MemoryList
DW SwitchVideoMode

KernelJump:
	DD  0
	DD  SegCo64
INTTBL_8616: DW 256 * 4 - 1; READONLY
	DD 0
INTTBL_8632: DW 0
	DD 0

; ---- ---- ---- ----
[BITS 16]
ALIGN 4
MemoryListData: TIMES 20*5 DD 0
MemoryList:
	MOV WORD[ADDR_PARA0], MemoryListData
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

[BITS 16]
CrtVideoInfo: TIMES 256 DB 0
SwitchVideoMode:
	XOR AX, AX
	MOV ES, AX
	MOV DI, CrtVideoInfo
	; Seek
	MOV AX, 0x4F01; VBE function: Get ModeInfo
	MOV CX, [ADDR_PARA1]
	INT 0x10
	CMP AX, 0x004F
	JNE SwitchVideoModeFail
	; Set Mode
	XOR AX, AX
	MOV DS, AX
	MOV AX, 0x4F02
	MOV BX, [ADDR_PARA1]
	OR  BX, 0x4000
	INT 0x10
	MOV WORD[ADDR_PARA0], CrtVideoInfo
RET
SwitchVideoModeFail:
	XOR AX, AX
	MOV DS, AX
	MOV WORD[ADDR_PARA0], 0
RET
