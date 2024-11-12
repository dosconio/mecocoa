; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240219
; AllAuthor: @dosconio
; ModuTitle: Kernel: Loader of Environment, Library and Shell
; Copyright: Dosconio Mecocoa, BSD 3-Clause License
; NegaiMap : { USYM --> Mecocoa --> USYM --> Any OS }

%include "pseudo.a"
%include "osdev.a"
%include "debug.a"
%include "mecocoa/kernel.inc"
; ---- ---- {Option Switch: 1 or others} ---- ----

EXTERN LoadFileMELF
EXTERN FloppyMotorOff
EXTERN PAGE_INIT

EXTERN _LoadFileMELF
EXTERN _InterruptInitialize
EXTERN _outtxt
EXTERN _outi32hex

GLOBAL HerMain
GLOBAL ModeProt32

[CPU 386]
%define _BSWAP_ALLOW_NOT;{TOBE} Limit since which?
DB "DOSCONIO"
[BITS 16]
KernelSymbols
; ---- ---- ---- ---- CODE ---- ---- ---- ----
section .text
; ENTRY 0x5808
HerMain:; assure CS,DS,ES,SS equal zero
; Initialize 16-Bit Real Mode
	DefineStack16 0, ADDR_STACKTOP
	XOR AX, AX
	MOV ES, AX
	MOV AX, CS
	MOV DS, AX
; Load IVT-Real
	MOV WORD [ES:80H*4+0], Routint16
	MOV WORD [ES:80H*4+2], CS
; Get and Check Memory Size (>= 4MB)
	;
; Register and Create 2 TSS of Shell for relative mode
	;{}
	MOV WORD[0x0500], ModeProt32
	MOV WORD[0x0502], CS
; Memory management and TSS Real-16 Store
	MOV WORD[0x052A], 0x8000
	MOV DWORD[0x052C], 0x00010000
	MOV [ADDR_TSS16+72], ES
	MOV [ADDR_TSS16+76], CS
	MOV [ADDR_TSS16+80], SS
	MOV [ADDR_TSS16+84], DS
	MOV [ADDR_TSS16+88], FS
	MOV [ADDR_TSS16+92], GS
	; ...
; Main - Load and Run Shell-Real16
	PUSH DS
	rprint str_progload
	MOV ECX, 0x1000;{TEMP}
	CALL MemAllocSuperv
	CALL MemAllocSuperv
	CMP EAX, 0x8000+0x1000
	JNZ Endo_Kernel_Real16
	; Load File
	loadfat ADDR_STATICLD, str_shell16
	PUSH WORD ADDR_STATICLD
	CALL LoadFileMELF
	ADD SP, 2
	CALL AX
	POP DS
	; Shell Real-16 Return then shutdown
Endo_Kernel_Real16:
	rprint str_endenv
	DbgStop
ModeProt32:
;{TEMP} Load Shell-32 here, for there are no Read Floppy Code in 32-bit
	loadfat 0x12000, str_shell32
	loadfat 0x32000, str_helloa
	loadfat 0x36000, str_hellob
	loadfat 0x3A000, str_helloc
	loadfat 0x44000, str_hellod
CALL FloppyMotorOff; ---- NEED NOT Floppy SINCE NOW ----
; Save state of Real-16 Shell and Enter Prot-32 Paged Flat Mode,
;     then Load and Run Shell-Prot32. Will not return in this procedure.
	rprinti32 .ento.prep.32
	rprint str_enter_32bit
.ento.prep.32:
	CLI
	CLD
; Paging
	CALL PAGE_INIT
; Setup basic GDT
	MOV EBX, ADDR_GDT
	; GDTE 00 NULL
	GDTDptrAppend 0,0
	; GDTE 01 Global Data and Stack - 00000000~FFFFFFFF (4GB)
	GDTDptrAppend 0x00CF9200,0x0000FFFF
	; GDTE 02 Global Code - 00000000~FFFFFFFF (4GB)
	GDTDptrAppend 0x00CF9A00,0x0000FFFF
	; GDTE 03 32-b Stack - 00003000~00003FFF (4KB) ; register but not use 
	GDTDptrAppend 0x00CF9600,0x4000FFFE
	OR DWORD[EBX-4], Linear
	; GDTE 04 Video display buffer 32K - 000B8000~000BFFFF (32KB) Ring(3; 0)
	GDTDptrAppend 0x0040F20B,0x80007FFF; 0x0040920B,0x80007FFF
	OR DWORD[EBX-4], Linear
	; GDTE 05 32PE ROUTINE (cancelled)
	ADD EBX, 8
	; GDTE 06 32PE CALL GATE
	PUSH EBX
	GateStruct RoutineGate,SegCode,1_11_011000000_0000B; P.DPL.01100000.PARA
	POP EBX
	GDTDptrAppend EDX,EAX
	;
	MOV [GDTPTR], EBX
	OR DWORD[GDTPTR], Linear
	SUB EBX, ADDR_GDT
	DEC EBX
	MOV  [GDTDptr], BX
	MOV  DWORD[GDTDptr+2], ADDR_GDT
	LGDT [GDTDptr]
; Enter 32-bit protected mode
	Addr20Enable; yo osdev.a
	MOV EAX, CR0
	OR EAX, 0x80000001; set PE bit and PG bit
	MOV CR0, EAX
	JMP DWORD SegCode: mainx+Linear
; 32 Return-to 16:
backpoint: 
	MOV EAX, CR0
	AND EAX, 0x7FFFFFFE; clear PE bit and PG bit
	MOV CR0, EAX
	JMP WORD 0:backinit; 16-bit instruction	
backinit:;{TODO} return shell16 to process
	; Initialize
	MOV DS, BX
	XOR BX, BX
	MOV ES, BX
	MOV SS, BX
	AND ESP, 0x0000FFFF
	Addr20Disable; yo osdev.a
	MOV WORD[IVTDptr], 0x3FF
	MOV DWORD[IVTDptr+2], 0x00000000
	lidt [IVTDptr]; zero
	rprint str_endenv
	DbgStop
	;JMP HerMain or ?
; ---- ---- ---- ---- RO16 ---- ---- ---- ----
%include "rout16.inc"
; ---- ---- ---- ---- CODEX ---- ---- ---- ----
[BITS 32]
mainx:
; Load segment registers
	MOV EAX, SegData
	MOV DS, EAX
	MOV FS, EAX
	MOV GS, EAX
	MOV ES, EAX
	MOV EAX, SegData; SegStack
	MOV SS, EAX
	MOVZX ESP, SP;XOR ESP, ESP
	OR ESP, Linear
	LGDT [GDTDptr+Linear]
	CLI; re-sure
; Setting up Interrupts
	xprinti32
	xprint str_load_ivt
	CALL DWORD _InterruptInitialize; i8259A_Init32
; Make base task
	; Continue GDT
	XOR EAX, EAX
	MOV EDI, [GDTPTR+Linear]
	MOV ECX, (0x10-0x07)*2; 0x10-0x07=0x09
	REP STOSD
	MOV [GDTPTR+Linear], EDI
	SUB EDI, (0x10-0x07)*2*4
	; Kernel TSS and its GDTE 7 - use null LDT
	MOV EAX, ADDR_TSS32+Linear
	MOV DWORD[EAX], 0; Backlink
	MOV ECX, CR3
	MOV DWORD[EAX+28], ECX; CR3(PDBR)
	MOV DWORD[EAX+96], 0; LDT
	MOV WORD[EAX+100], 0; T=0 , is what ???
	MOV WORD[EAX+102], 103; NO I/O BITMAP
    PUSH EAX
    PUSH EDI; into EBX
    MOV EBX,104-1
    MOV ECX,0x00408900
	MOV EDI, RotDptrStruct
	CALL SegGate:0
    POP EBX
    GDTDptrAppend EDX,EAX
    POP EAX
	SUB EBX, ADDR_GDT+Linear
	DEC EBX
	MOV DWORD[GDTPTR+Linear], ADDR_GDT+Linear+0x80
	MOV WORD[GDTDptr+Linear], 8*0x10-1;BX
	LGDT [GDTDptr+Linear]
	MOV CX, SegTSS
	LTR CX
	ADD WORD[EAX+2], 1; state
;{} [Debug Area]
	JMP mainx.load.shell32
Abort_Kernel_Flap32:
	DbgStop
; Mainx - Load and Run Shell-Prot32
mainx.load.shell32:
	xprinti32
	xprint str_load_shell32
	; Buffer Area
	MOV EDI, RotMalloc
	MOV ECX, 0x10000; buffer and Shell32
	CALL SegGate:0
	MOV EBX, DWORD[0x8000052C]; dbg
	CMP EAX, 0x10000
	JNZ Abort_Kernel_Flap32; abort
	POP EAX
	; Load Shell32
	PUSH DWORD 0x12000
	CALL _LoadFileMELF
	CALL EAX; return then return real-16
	xprint str_return_from_shell32
	xprinti32 EAX
	xprint str_newline
ReturnReal16:
	CLI
	xprint str_quit_32bit
	;
	MOV DWORD[0x0510], 0
	MOV DWORD[0x0514], 0
	MOV BX, [ADDR_TSS16+84]
	MOV AX, SegData
	MOV DS, AX
	MOV ES, AX
	MOV FS, AX
	MOV GS, AX
	AND DWORD[ADDR_GDT+Linear+0x10+4], 0x0FBFFFFF; Code Segment
	AND DWORD[ADDR_GDT+Linear+0x18+4], 0x0F30FFFF; Stak Segment
	AND DWORD[ADDR_GDT+Linear+0x18+0], 0xFFFF0000; Stak Segment
	MOV EAX, SegStack
	MOV SS, EAX
	JMP DWORD SegCode:backpoint; no Linear
; ---- ---- ---- ---- RO32 ---- ---- ---- ----
%include "rout32.inc"
; ---- ---- ---- ---- DATA ---- ---- ---- ----
section .data
	str_progload:
		DB "Loading the shell of real-16"
		%ifdef _FLOPPY
			DB "(F)"
		%endif
		DB "...", 10, 13, 0
	str_endenv:
		DB "End of the soft environment.", 10, 13, 0
	str_endenv_dbg:
		DB "End of debug!", 10, 13, 0
	str_test_filename:
		DB "TEST    TXT"
	str_shell16:
		DB "SHL16   APP"
	str_shell32:
		DB "SHL32   APP"
	str_helloa:
		DB "HELLOA  APP"
	str_hellob:
		DB "HELLOB  APP"
	str_helloc:
		DB "HELLOC  APP"
	str_hellod:
		DB "HELLOD  APP"
	str_enter_32bit:
		DB " - Entering 32-bit protected mode...",10,13,0
	str_load_ivt:
		DB " - Setting up Interrupts...",10,13,0
	str_load_shell32:
		DB " - Loading the shell of prot-32",10,13,0
	str_return_from_shell32:
		DB 10,13,"Shell32 return: ",0
	str_used_mem:
		DB "Used Memory: ",0
	str_newline:
		DB 10,13,0
	str_quit_32bit:
		DB "Quit 32-bit protected mode...",10,13,0
	msg_spaces:
		DB "        ",0
	msg_error:
		DB "Error!",10,13,0
	;
	GDTPTR: DD 0
	SvpAllocPtr: DD 0x0000A000;(to cancel this) Supervisor
	UsrAllocPtr: DD 0x00100000
	TSSNumb: DW 1
		DD 0; of TSSCrt
	TSSCrt: DW 0; Kernel's=0
	TSSMax: DW 0

; ---- ---- ---- ---- KERNEL 32 ---- ---- ---- ----
ALIGN 32
[BITS 32]

; Endf
