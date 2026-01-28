; ASCII
; Format: Binary

%include "osdev.a"
%include "ladder.a"

; Link this below or above 0x10000
%if _MCCA == 0x8664
ORG 0x8000
%endif

SegData EQU 8*1
SegCo32 EQU 8*2
SegCo16 EQU 8*4
SegCo64 EQU 8*5

ADDR_PARA0 EQU 0x500
ADDR_PARA1 EQU 0x502

%if _MCCA == 0x8664
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

%elif _MCCA == 0x8632

[BITS 32]
GLOBAL CallCo16
CallCo16:
	MOV EAX, [ESP+4*1]
	MOV WORD[0x500], AX
	; flap32 -> real16
	PUSHAD
	JMP DWORD 8*4:PointReal16;{} can do this directly
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

%endif

; ---- ---- . ---- ----

FUNC_CNTS EQU 0x3
; FUNCTIONS
; 0x00 MemoryList(->addr)
; 0x01 SwitchVideoMode(mode->addr)
; 0x02 VideoModeList()
FUNC_TABLE:
DW MemoryList
DW SwitchVideoMode
DW VideoModeList

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

[BITS 16]; by ArinaMgk
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

[BITS 16]; by ArinaMgk
CrtVideoModeInfo: TIMES 512 DB 0
VideoModeListPointer: DW 0
VideoModeList:; -> 0x78000
	XOR AX, AX
	MOV ES, AX
	MOV [VideoModeListPointer], AX
	MOV EAX, 0x4F00; Get VBE Controller Info
	MOV DI, CrtVideoModeInfo
	INT 0x10
	CMP AX, 0x004F
	JNE SwitchVideoModeFail; <!> VBE Not Support
	MOV SI, [CrtVideoModeInfo + 14]
	MOV AX, [CrtVideoModeInfo + 16]
	MOV FS, AX
	MOV AX, 0x7800
	MOV GS, AX
	VideoModeListLoop:
		MOV CX, [FS:SI]
		CMP CX, 0xFFFF
		JZ  VideoModeListFin
		PUSH SI
		;
		MOV AX, 0x4F01; VBE function: Get ModeInfo
		MOV DI, CrtVideoInfo
		INT 0x10
		CMP AX, 0x004F
		JNE VideoModeListNext;
		;
		MOV  AX, [CrtVideoInfo + 0]
		TEST AL, 1; Hardware Support
		JZ   VideoModeListNext
		MOV  AX, [CrtVideoInfo + 0x1B]
		; filter DirectColor Modes and ARGB
		CMP  AL, 6; TEXT(0) INDEX(4) DIRECT(6)
		JNZ  VideoModeListNext
		; Simple judge ARGB
		MOV  AL, [CrtVideoInfo + 0x24]; BlueFieldPosition
		OR   AL, AL
		JNZ  VideoModeListNext
		; log
		MOV BX, [VideoModeListPointer]
		MOV AX, [FS:SI]
		MOV [GS:BX], AX
		MOV AX, [CrtVideoInfo + 0x12]; XResolution
		MOV [GS:BX+2], AX
		MOV AX, [CrtVideoInfo + 0x14]; YResolution
		MOV [GS:BX+4], AX
		MOV AL, [CrtVideoInfo + 0x23]; BlueMaskSize
		MOV AH, [CrtVideoInfo + 0x21]; GreenMaskSize
		SHL AH, 4
		OR  AH, AL
		MOV [GS:BX+6], AH
		MOV AL, [CrtVideoInfo + 0x1F]; RedMaskSize
		MOV AH, [CrtVideoInfo + 0x25]; RsvdMaskSize
		SHL AH, 4
		OR  AH, AL
		MOV [GS:BX+7], AH
		ADD BX, 8
		CMP BX, 0x8000
		JAE VideoModeListFin0
		MOV [VideoModeListPointer], BX
VideoModeListNext:
	POP SI
	ADD SI, 2
	JMP VideoModeListLoop
VideoModeListFin0:
	POP SI
VideoModeListFin:
	RET
