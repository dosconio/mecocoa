

%macro defrotx 1
	DD %1+Linear
	DW SegCode,0
%endmacro

;[Routines protect-32 mode]
APISymbolTable:; till Routine
	RoutineNo: DD (__Routine-rot0000)/8
	rot0000: defrotx R_Print; PrintString
	rot0001: defrotx R_PrintDwordCursor; PrintDwordCursor
	rot0002: defrotx R_Malloc; Malloc
	rot0003: defrotx R_Mfree; Mfree (waiting for adding)
	rot0004: defrotx R_DiskReadLBA28; DiskReadLBA28
	TIMES 11*2 DD 0;5,6,7,8,9,A,B,C,D,E,F
	rot0010: defrotx R_DescriptorStructure; DescriptorStructure
__Routine:
RoutineGate:; EDI=FUNCTION RoutIn:{DS=SegData}
	CMP EDI, [RoutineNo+Linear]
	JAE R_Terminate
	CALL FAR [(rot0000+Linear)+EDI*8]
	RETF
R_Terminate:; 00 and other rontine point to here
	PUSHFD
	POP EDX
	TEST DX, 0100_0000_0000_0000B
	JNZ NESTED_TASK
	JMP SegTSS:0
	RETF
;	JMP GeneralExceptionHandler
	NESTED_TASK: IRETD
	RETF; for next calling the subapp
	ALIGN 16
; ----
R_Print:
	PUSHAD
	PUSH DWORD ~0
	PUSH ESI
	CALL DWORD _outtxt
	ADD ESP, 4*2
	POPAD
	RETF
	ALIGN 16
R_Malloc:; ecx=length ret"eax=start" (user-area by manager)
	; return [THISF_ADR+RamAllocPtr]++
;	PUSH EBX
;	OR ECX, ECX
;	JZ R_MemAlloc_End
;	MOV EAX, [THISF_ADR+UsrAllocPtr]
;	MOV EBX, EAX
;	ADD EBX, ECX
;	CMP EBX, 0x00400000; UserRight
;	JBE R_MemAlloc_Next
;	XOR EAX, EAX
;	JMP R_MemAlloc_End
;	R_MemAlloc_Next:
;	ADD DWORD[THISF_ADR+UsrAllocPtr], ECX
;	DEC EBX
;	AND EBX,0xFFFFF000
;	R_MemAlloc_Loop:
;		CMP EBX, EAX
;		JB R_MemAlloc_End
;		CALL RR_PageRegisterUser
;		CMP EBX, 0
;		JZ R_MemAlloc_End
;		SUB EBX, 0x1000
;		JMP R_MemAlloc_Loop
;
;	R_MemAlloc_End:
;	POP EBX
;	RETF
;	RR_PageRegisterUser:
;		PageRegisterUser
;		RET
	ALIGN 16
R_Mfree:
	;; ...
	RETF
	ALIGN 16
R_DiskReadLBA28:;eax=start, ds:ebx=buffer
;	HdiskLoadLBA28 EAX,1
;	RETF
;	ALIGN 16
R_PrintDwordCursor:
	PUSHAD
	PUSH EDX
	CALL DWORD _outi32hex
	POP EDX
	POPAD
	RETF
	ALIGN 16


R_DescriptorStructure:
	GDTDptrStruct EAX,EBX,ECX
	RETF
	ALIGN 16

RoutineEnd:

