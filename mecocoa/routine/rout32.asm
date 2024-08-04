GLOBAL _MccaLoadGDT
GLOBAL _MccaAlocGDT
GLOBAL RoutineGate

%include "osdev.a"
%include "debug.a"
%include "mecocoa/kernel.inc"
%include "include/console.inc"
[CPU 586]
KernelSymbols
[BITS 32]

; return the index but how many GDTs.
_MccaLoadGDT:
	MOVZX EAX, WORD[0x80000500+0x4]
	INC EAX
	SHR EAX, 3 ; 
	DEC EAX
	LGDT [0x80000500+0x4]
RET

_MccaAlocGDT:
	ADD WORD[0x80000500+0x4], 8
JMP _MccaLoadGDT


; ----
%macro defrotx 1
	DD %1+Linear
	DW SegCode,0
%endmacro

;[Routines protect-32 mode]
APISymbolTable:; till Routine, also as ABI
	RoutineNo: DD (__Routine-rot0000)/8
	rot0000: defrotx R_Print; PrintString
	rot0001: defrotx R_PrintDwordCursor; PrintDwordCursor
	rot0002: defrotx R_Malloc; Malloc
	rot0003: defrotx R_Mfree; Mfree (waiting for adding)
	rot0004: defrotx R_DiskReadLBA28; DiskReadLBA28
	rot0005: dd      0,0
	rot0006: defrotx R_SysDelay
	TIMES 8*2 DD 0; 7,8,9,A,B,C,D,E
	rot000F: defrotx R_Terminate;
	rot0010: defrotx R_DescriptorStructure; DescriptorStructure
	rot0011: defrotx R_TEMP_OpenInterrupt
__Routine: 
RoutineGate:; EDI=FUNCTION RoutIn:{DS=SegData}
	PUSH DS
	PUSH ES
	PUSH AX
	MOV AX, SegData
	MOV DS, AX
	MOV ES, AX
	POP AX
	; keep FS and GS
	CMP EDI, [RoutineNo+Linear]
	JAE R_Terminate
	CALL FAR [(rot0000+Linear)+EDI*8]
	POP ES
	POP DS
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
	PUSH ECX
	CALL _memalloc
	POP ECX
	RETF
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
R_SysDelay:
	PUSHAD
	MOVZX EAX, WORD[0x80000528]
	_wait_loop:
		HLT
		MOVZX EBX, WORD[0x80000528] ; ms
		SUB BX, AX
		CMP EBX, EDX
		JB _wait_loop
	POPAD
	RETF
	ALIGN 16

R_DescriptorStructure:
	GDTDptrStruct EAX,EBX,ECX
	RETF
	ALIGN 16
R_TEMP_OpenInterrupt:
	STI
	RETF
	ALIGN 16


RoutineEnd:

