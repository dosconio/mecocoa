; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; Enter 32-bit protected mode and print a message
;{TODO RENAME} Kernel32.asm

; will be loaded at 0x2000

%include "pseudo.a"
%include "osdev.a"
%include "debug.a"
%include "video.a"

THISF_ADR EQU 0x2000;{TEMP} Constant

SEG16_GDT EQU 0x0708
STACK_LEN EQU 0x1000
GDT_ADDR  EQU 0x7080

SegNull   EQU (8*0)
SegData   EQU (8*1)
SegCode   EQU (8*2)
SegStack  EQU (8*3)
SegVideo  EQU (8*4)
SegRot    EQU (8*5)
SegGate   EQU (8*6)
SegTSS    EQU (8*7)
SegShell  EQU (8*8); TSS
SegShellT EQU (8*9); LDT

RotPrint EQU 1

[CPU 386]
File

CLI

MOV [para_cs], CS
MOV [para_ds], DS
MOV WORD[quit_addr], quit

MOV SI, msg
MOV AH, 0
INT 80H

; Setup basic GDT
PUSH DS
PUSH WORD [para_cs]
MOV SI, msg_setup_basic_gdt
;[OPT] MOV AH, 0
INT 80H
MOV AX, SEG16_GDT
MOV DS, AX
XOR EBX, EBX
	; GDTE 00 NULL
	GDTDptrAppend 0,0
	; GDTE 01 Global Data - 00000000~FFFFFFFF (4GB)
	GDTDptrAppend 0x00CF9200,0x0000FFFF
	; GDTE 02 Current Code
	XOR EAX, EAX
	POP AX
	PUSH EBX
	SHL EAX, 4
	MOV EBX, Endfile
	DEC EBX
	MOV ECX,0x00409800
	CALL F_GDTDptrStruct
	POP EBX
	GDTDptrAppend EDX,EAX
	; GDTE 03 32-b Stack - 00006000~00006FFF (4KB)
	GDTDptrAppend 0x00CF9600,0x7000FFFE
	; GDTE 04 Video display buffer 32K - 000B8000~000BFFFF (32KB)
	GDTDptrAppend 0x0040920B,0x80007FFF
	; GDTE 05 32PE ROUTINE
	PUSH EBX
	MOV EAX, THISF_ADR+__Routine
	MOV EBX, RoutineEnd-__Routine-1
	MOV ECX, 0x00409800
	CALL F_GDTDptrStruct
	POP EBX
	GDTDptrAppend EDX,EAX
	; GDTE 06 32PE CALL GATE
	PUSH EBX
	GateStruct (RoutineGate-__Routine),SegRot,1_11_011000000_0000B; P.DPL.01100000.PARA
	POP EBX
	GDTDptrAppend EDX,EAX
POP DS
MOV [GDTPTR], EBX
ADD DWORD[GDTPTR], GDT_ADDR
DEC EBX; Now equal size of GDT
MOV WORD[GDTable], BX
LGDT [GDTable]

; Enter 32-bit protected mode
MOV SI, msg_enter_32bit
MOV AH, 0
INT 80H

Addr20Enable; yo osdev.a

MOV EAX, CR0
OR EAX, 1; set PE bit
MOV CR0, EAX

JMP DWORD 8*2:mainx

[BITS 32];--------------------------------
mainx:

; Load segment registers
MOV EAX, 8*1
MOV DS, EAX
MOV EAX, 8*4
MOV ES, EAX
MOV EAX, 8*3
MOV SS, EAX
MOV ESP, 0;STACK_LEN


MOV ESI, THISF_ADR+msg_load_ivt
MOV EDI, RotPrint
CALL SegGate:0

DbgStop

; Restore information of segments into registers
;{TODO} Print quit message
MOV BX, [para_ds]

MOV EAX, CR0
AND EAX, 0xFFFFFFFE; clear PE bit
MOV CR0, EAX

MOV DS, BX

JMP FAR [DS:quit_addr]

[BITS 16];--------------------------------
quit:
;{TODO Learn} Back to real-16 mode
Addr20Disable; yo osdev.a

; Return to real-16 mode
MOV SI, msg_return_to_real16
MOV AH, 0
INT 80H
;...

; Return to base environment
RETF

;[Procedure real-16 mode]
; ---- Structure Segment Selector ----
F_GDTDptrStruct:
	%define _BSWAP_ALLOW_NOT
	GDTDptrStruct EAX,EBX,ECX
	RET

quit_addr: DW 0
para_cs: DW 0
para_ds: DW 0
GDTPTR: DD 0
GDTable:
	DW 0
	DD GDT_ADDR
msg: DB "Ciallo, Mecocoa~",10,13,0
msg_setup_basic_gdt: DB "Setting up basic GDT...",10,13,0
msg_return_to_real16: DB "Return to real-16 mode...",10,13,0
msg_enter_32bit: DB "Enter 32-bit protected mode...",10,13,0
msg_load_ivt: DB "Load IVT...",10,13,0


msg_quit_32bit: DB "Quit 32-bit protected mode...",10,13,0

;[Procedure protect-32 mode]
[BITS 32];--------------------------------
APISymbolTable:; till Routine
	RoutineNo: DD (__Routine-rot0000)/8
	rot0000:; Terminate
		DD R_Terminate-__Routine
		DW SegRot,0
	rot0001:; PrintString (accept New Line)
		DD R_Print-__Routine
		DW SegRot,0
	rot0002:; Malloc
		DD R_Malloc-__Routine
		DW SegRot,0
	rot0003:; (waiting for adding)
		DD R_Mfree-__Routine
		DW SegRot,0
	rot0004:; DiskReadLBA28
		DD R_DiskReadLBA28-__Routine
		DW SegRot,0
__Routine:
RoutineGate:; EDI=FUNCTION
	PUSHAD
	PUSH DS
	PUSH EAX
	MOV EAX, SegData
	MOV DS,EAX
	POP EAX
	MOV EAX, EDI
	CMP DWORD[THISF_ADR+RoutineNo], EAX
	JB RoutineGateEnd
	SHL EAX, 3; MUL8
	CALL FAR [THISF_ADR+rot0000+EAX]
	RoutineGateEnd:
	POP DS
	POPAD
	RETF
R_Terminate:; 00 and other rontine point to here
	;;	POP EDX; STORE
	;;	POP DS; STORE
	;;	POPAD; STORE
	;;	PUSHFD
	;;	POP EDX
	;;	TEST DX,0100_0000_0000_0000B
	;;	JNZ NESTED_TASK
	;;
	;;	JMP SegTSS:0
	;;	RETF
	;;
	;;	WaitRemoved: HLT
	;;	JMP WaitRemoved
	;;	NESTED_TASK: IRETD
	RETF; for next calling the subapp
	ALIGN 16
R_Print:; INPUT=DS:ESI
	PUSH ES
	PUSH EAX
	PUSH EBX
	PUSH AX	
	MOV EAX, SegVideo
	MOV ES,EAX
	POP AX
	ConPrint ESI,~
	POP EBX
	POP EAX
	POP ES
	RETF
	ALIGN 16
R_Malloc:; ecx=length ret"eax=start"
	;;	PUSH DS
	;;	MOV EAX,8*1
	;;	MOV DS,EAX
	;;	MOV EAX,[DS:THISF_ADR+RamAllocPtr]
	;;	ADD DWORD[DS:THISF_ADR+RamAllocPtr],ECX
	;;	POP DS
	RETF
	ALIGN 16
R_Mfree:
	;; ...
	RETF
	ALIGN 16
R_DiskReadLBA28:;eax=start, ds:ebx=buffer
	;;	HdiskLoadLBA28 EAX,1 (unchecked)
	RETF
	ALIGN 16
RoutineEnd:

Endf
Endfile:
