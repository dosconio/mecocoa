; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240219
; AllAuthor: @dosconio
; ModuTitle: Kernel: Loader of Environment, Library and Shell
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

%include "pseudo.a"
%include "debug.a"
%include "osdev.a"
%include "video.a"
%include "demos.a"
%include "cokasha/kernel.inc"
; ---- ---- {Option Switch: 1 or others} ---- ----
IVT_TIMER_ENABLE EQU 1
MEM_PAGED_ENABLE EQU 1

EXTERN ReadFileFFAT12
EXTERN LoadFileMELF
EXTERN FloppyMotorOff
EXTERN PAGE_INIT

EXTERN _LoadFileMELF
EXTERN i8259A_Init32
EXTERN _outtxt
EXTERN _outi32hex

GLOBAL HerMain
GLOBAL ModeProt32

[CPU 386]
DB "DOSCONIO"
[BITS 16]
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
; TSS Real-16 Store
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
	PUSH WORD ADDR_KERNELBF; Buffer
	PUSH WORD str_shell16;{TODO} if not found
	PUSH WORD ADDR_STATICLD
	CALL ReadFileFFAT12
	CALL LoadFileMELF; make use of the stack
	ADD SP, 6
	CALL AX
	POP DS
	; Shell Real-16 Return then shutdown
; Endo
	rprint str_endenv
	DbgStop
ModeProt32:
;{TEMP} Load Shell-32 here, for there are no Read Floppy Code in 32-bit
	PUSH ES
	MOV AX, 0x1000
	MOV ES, AX
	PUSH WORD 0; ES:Buffer
	PUSH WORD str_shell32;{TODO} if not found
	PUSH WORD 0x2000; ES
	CALL ReadFileFFAT12
	CALL FloppyMotorOff; ---- NEED NOT Floppy SINCE NOW ----
	ADD SP, 6
	POP ES
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
	%if MEM_PAGED_ENABLE!=0
		OR DWORD[EBX-4], Linear
	%endif
	; GDTE 04 Video display buffer 32K - 000B8000~000BFFFF (32KB) Ring(3; 0)
	GDTDptrAppend 0x0040F20B,0x80007FFF; 0x0040920B,0x80007FFF
	%if MEM_PAGED_ENABLE!=0
		OR DWORD[EBX-4], Linear
	%endif
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
backinit:
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
%include "cokasha/rout16.asm"
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
; [Optional] Load IVT
	MOV EDI, RotEchoDword
	MOV EDX, .load.IVT
	CALL SegGate:0
	MOV EDI, RotPrint
	MOV ESI, str_load_ivt+Linear
	CALL SegGate:0
	; The former 20 is for exceptions
.load.IVT:
	GateStruct GeneralExceptionHandler,SegCode,0x8E00; 32BIT R0
	MOV EDI, ADDR_IDT32+Linear
	MOV ECX, 20; 20*8=0x0A0
	rcpgates
	; Then for interruption (256-20)
	GateStruct GeneralInterruptHandler,SegCode,0x8E00; 32BIT R0
	MOV ECX,256-20; 256*8=0x800
	rcpgates
	; Setup timer
	GateStruct Timer_70HINTHandler,SegCode,0x8E00; 32BIT R0
	MOV [ADDR_IDT32+8*0x70], EAX
	MOV [ADDR_IDT32+8*0x70+4], EDX
	; Load IDT
	SUB EDI, ADDR_IDT32+Linear+1
	MOV EAX, EDI
	MOV  WORD[IVTDptr+Linear], AX
	MOV DWORD[IVTDptr+Linear+2], ADDR_IDT32
	LIDT [IVTDptr+Linear]
	%if IVT_TIMER_ENABLE!=0
	; Enable 8259A
	MOV EDI, RotPrint
	MOV ESI, str_enable_i8259a+Linear
	CALL SegGate:0
	CALL DWORD i8259A_Init32
	%endif
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
    CALL F_GDTDptrStruct_32
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
; [Debug Area]
	JMP .load.shell32
	EXTERN _curset
	EXTERN _curget
	CALL _curget
	ADD EAX, 2
	PUSH EAX
	CALL _curset
	POP EAX
	DbgStop
; Mainx - Load and Run Shell-Prot32
.load.shell32:
	MOV EDI, RotPrint
	MOV ESI, str_load_shell32+Linear
	CALL SegGate:0
	PUSH DWORD 0x12000
	CALL _LoadFileMELF
	CALL EAX; then return real-16
;;	MOV WORD[THISF_ADR+TSSCrt], 8*0x11
;;	MOV WORD[THISF_ADR+TSSMax], 8*0x11
;;	MOV ESI, THISF_ADR+msg_load_shell32
;;	MOV EDI, RotPrint
;;	CALL SegGate:0
;;	PUSH DWORD 20
;;	CALL F_TSSStruct3
; {OPT TEST} Load Subapp a and b
;;	PUSH DWORD 50
;;	ADD WORD[THISF_ADR+TSSMax], 2*8; {LDT+TSS}*8
;;	CALL F_TSSStruct3
;;	PUSH DWORD 60
;;	ADD WORD[THISF_ADR+TSSMax], 2*8; {LDT+TSS}*8
;;	CALL F_TSSStruct3
; {OPT TEST} Echo User Area
;;	MOV EDI, RotPrint
;;	MOV ESI, THISF_ADR+msg_used_mem
;;	CALL SegGate:0
;;	MOV EDI, RotEchoDword
;;	MOV EDX, [THISF_ADR+UsrAllocPtr]
;;	CALL SegGate:0
;;	MOV EDI, RotPrint
;;	MOV ESI, THISF_ADR+msg_newline
;;	CALL SegGate:0
; {OPT TEST} Main loop
;;	MOV ECX, 500
;;	Shell32_Test:
;;	CALL 8*0x11:0x00000000; After CLI before you try this
;;	MOV EDI, RotPrint
;;	MOV ESI, THISF_ADR+msg
;;	CALL SegGate:0
;;	; LOOP Shell32_Test
; Enable Multi-tasks
	;;STI
ReturnReal16:
	CLI
	MOV EDI, RotPrint
	MOV ESI, str_quit_32bit+Linear
	CALL SegGate:0
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
%include "cokasha/rout32.asm"
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
	str_enter_32bit:
		DB " - Entering 32-bit protected mode...",10,13,0
	str_load_ivt:
		DB " - Loading IVT32...",10,13,0
	str_enable_i8259a:
		DB "Enabling i8259A...",10,13,0
	str_load_shell32:
		DB "Loading the shell of prot-32",10,13,0
	str_used_mem:
		DB "Used Memory: ",0
	str_newline:
		DB 10,13,0
	str_quit_32bit:
		DB "Quit 32-bit protected mode...",10,13,0

	msg_spaces:
		DB "        ",0
	msg_on_1s:
		DB "<Ring~> ",0
	msg_general_exception:
		DB "General Exception!",10,13,0
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
