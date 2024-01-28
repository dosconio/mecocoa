; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
; Enter 32-bit protected mode and print a message

%include "pseudo.a"
%include "osdev.a"
%include "debug.a"
%include "video.a"
%include "hdisk.a"
%include "demos.a"
%include "_debug.a"

; {Option Switch: 1 or others}
IVT_TIMER_ENABLE EQU 1
MEM_PAGED_ENABLE EQU 1

%include "offset.a" ; addresses and selectors
%include "routidx.a"; index of routines

[CPU 386]
File
; Initial
	CLI
	MOV SI, msg
	MOV AH, 0
	INT 80H
	MOV [para_cs], CS
	MOV [para_ds], DS
	MOV [para_ss], SS
	MOV [para_sp], SP

; Paging keep(DS:0x0000)
	PUSH DS
	%if MEM_PAGED_ENABLE==1
	MOV SI, msg_paging
	MOV AH, 0
	INT 80H
	; Clear GPDT to make use of Protect
	XOR AX, AX
	MOV DS, AX
	MOV CX, 1024; GPDT_NUM (1024*4=4KB)
	MOV SI, PDT_ADDR
	PDTFill0:
		MOV DWORD[SI], EAX
		ADD SI, 4
		LOOP PDTFill0
	; Index GPDT self
	MOV DWORD[PDT_ADDR+0x1000-4], 0x00005003; US=S RW P 00005000~00005FFF FFFFF000~FFFFFFFF
	%endif
	; PgTable of Shell32
	MOV DWORD[PDT_ADDR+0x1000/2], 0x00008003; US=S RW P 00008000~00008FFF 80008000~80008FFF
	; Transition for. This will be removed later.
	MOV DWORD[PDT_ADDR], 0x00008003
	; Fill Bitmap
		MOV AX, SEG16_MAP
		MOV ES, AX
		XOR DI, DI
		MOV BYTE[ES:DI], 0xFF
		INC DI
		MOV BYTE[ES:DI], 0b00000011
		INC DI
		; Kernel Area
		MOV CX, (16-2)/2
		XOR AX, AX
		REP STOSW
		; BIOS Area
		MOV CX, 16/2
		XOR AX, 0xFFFF
		REP STOSW
		; User Area
		MOV CX, 16*6/2
		XOR AX, AX
		REP STOSW
		; Omit the higher (00400000~FFFFFFFF)
	; Fill Kernel PgTable
	MOV SI, KPT_ADDR
	MOV CX, 0x100; 0~0x100'000 , last one: 0x83fc:0x000ff003
	MOV EAX, 3; US=S RW P
	loop_fill_kpt:
		MOV DWORD[SI], EAX
		ADD SI, 4
		ADD EAX, 0x1000; 4MB
		LOOP loop_fill_kpt
	; Apply
	MOV EAX, PDT_ADDR
	MOV CR3, EAX
	POP DS
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
		%if MEM_PAGED_ENABLE==1
			OR EAX, 0x80000000
		%endif
		MOV EBX, Endfile
		DEC EBX
		MOV ECX,0x00409800
		CALL F_GDTDptrStruct
		POP EBX
		GDTDptrAppend EDX,EAX
		; GDTE 03 32-b Stack - 00006000~00006FFF (4KB)
		GDTDptrAppend 0x00CF9600,0x7000FFFE
		%if MEM_PAGED_ENABLE==1
			OR DWORD[EBX-4], 0x80000000
		%endif
		; GDTE 04 Video display buffer 32K - 000B8000~000BFFFF (32KB) Ring(3; 0)
		GDTDptrAppend 0x0040F20B,0x80007FFF; 0x0040920B,0x80007FFF
		%if MEM_PAGED_ENABLE==1
			OR DWORD[EBX-4], 0x80000000
		%endif
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
	; ; now DS = Original DS ; ;
	MOV [GDTPTR], EBX
	ADD DWORD[GDTPTR], GDT_LDDR
	DEC EBX; Now equal size of GDT
	MOV WORD[GDTable], BX
	LGDT [GDTable]

; Enter 32-bit protected mode
	MOV SI, msg_enter_32bit
	MOV AH, 0
	INT 80H

	Addr20Enable; yo osdev.a
	MOV EAX, CR0
	OR EAX, 0x80000001; set PE bit and PG bit
	MOV CR0, EAX
	JMP DWORD SegCode: mainx

[BITS 32]
mainx:
; Load segment registers
	CLI; {REASSURE}
	MOV EAX, SegData
	MOV DS, EAX
	MOV EAX, SegVideo
	MOV ES, EAX
	MOV FS, EAX
	MOV GS, EAX
	MOV EAX, SegStack
	MOV SS, EAX
	XOR ESP, ESP
	LGDT [THISF_PH+GDTable]
; Trifle
	; Transition for. Now remove it.
	;{TODO}MOV DWORD[PDT_LDDR], 0x0000067
	OR DWORD[PDT_LDDR], 7
	
; [Optional] Load IVT
	MOV ESI, THISF_ADR+msg_load_ivt
	MOV EDI, RotPrint
	CALL SegGate:0
	; The former 20 is for exceptions
	GateStruct GeneralExceptionHandler,SegCode,0x8E00; 32BIT R0
	MOV ESI, IDT_ADDR
	MOV ECX, 20; 20*8=0x0A0
	_IDTFill0:
		MOV [ESI], EAX
		ADD ESI,4
		MOV [ESI], EDX
		ADD ESI,4
		LOOP _IDTFill0
	; Then for interruption (256-20)
	GateStruct GeneralInterruptHandler,SegCode,0x8E00; 32BIT R0
	MOV ECX,256-20; 256*8=0x800
	_IDTFill1:
		MOV [ESI], EAX
		ADD ESI,4
		MOV [ESI], EDX
		ADD ESI,4
		LOOP _IDTFill1
	; Setup timer
	GateStruct Timer_70HINTHandler,SegCode,0x8E00; 32BIT R0
	MOV [IDT_ADDR+8*0x70], EAX
	MOV [IDT_ADDR+8*0x70+4], EDX
	; Load IDT
	SUB ESI, IDT_ADDR+1
	MOV EAX, ESI
	MOV [THISF_ADR+IVTDptr], AX
	LIDT [THISF_ADR+IVTDptr]
	%if IVT_TIMER_ENABLE==1
	; Enable 8259A
	; ; Master PIC
	MOV AL, 10001B; ICW1 {ICW1EN, EdgeTrigger, 8b-ADI, !Single, ICW4EN}
	OUT 0X20, AL
	MOV AL, 0X20; ICW2: Relative Vector Address
	OUT 0X21, AL
	MOV AL, 00000100B; ICW3: P-2 Connected to Slave
	OUT 0X21, AL
	MOV AL, 00001B; ICW4 {!SFNM, !BUF, Slave(Unused?), ManualEOI, Not8B}
	OUT 0X21, AL
	; ; Slave PIC
	MOV AL, 10001B; ICW1 {ICW1EN, EdgeTrigger, 8b-ADI, !Single, ICW4EN}
	OUT 0XA0, AL
	MOV AL, 0X70; ICW2: Relative Vector Address
	OUT 0XA1, AL
	MOV AL, 00000100B; ICW3: P-2 Connected to Slave
	OUT 0XA1, AL
	MOV AL, 00001B; ICW4 {!SFNM, !BUF, Slave(Unused?), ManualEOI, Not8B}
	OUT 0XA1, AL
	; Enable Timer
	MOV AL, 0X0B; RTC Register B
	OR AL, 0X80; Block NMI
	OUT 0X70, AL
	MOV AL, 0X12;Set Reg-B {Ban Periodic, Open Update-Int, BCD, 24h}
	OUT 0X71, AL;
	IN AL, 0XA1; Read IMR of Slave PIC
	AND AL, 0XFE; Clear BIT 0, which is linked with RTC
	OUT 0XA1, AL; Write back to IMR
	MOV AL, 0X0C
	OUT 0X70, AL
	IN AL, 0X71; Read Reg-C, reset pending interrupt
	;{TODO} Check PIC Device
	PIC_CHECK:
	%endif

; Make base task (DS base = 0x00000000)
	MOV ESI, THISF_ADR+msg_make_base_task
	MOV EDI, RotPrint
	CALL SegGate:0
	; Continue GDT
	MOV EBX, [THISF_ADR+GDTPTR]
	MOV ECX, (0x10-0x07)*2; 0x10-0x07=0x09
	_GDTDptrAppend_0:; 0x07~0x0F
		MOV DWORD[EBX], 0
		ADD EBX, 4
		LOOP _GDTDptrAppend_0
	MOV [THISF_ADR+GDTPTR], EBX
	SUB EBX, (0x10-0x07)*2*4
	; Kernel TSS and its GDTE 7
	; ; use null LDT, or use zero-value LDT yo GDT
	MOV EAX, TSS_LDDR
	MOV DWORD[EAX], 0; Backlink
	MOV ECX, CR3
	MOV DWORD[EAX+28], ECX; CR3(PDBR)
	MOV DWORD[EAX+96], 0; LDT
	MOV WORD[EAX+100], 0; T=0 , is what ???
	MOV WORD[EAX+102], 103; NO I/O BITMAP
    PUSH EAX
    PUSH EBX
    MOV EBX,104-1
    MOV ECX,0x00408900
    CALL F_GDTDptrStruct_32
    POP EBX
    GDTDptrAppend EDX,EAX
    POP EAX
	SUB EBX, GDT_LDDR
	DEC EBX
	MOV DWORD[THISF_ADR+GDTPTR], GDT_LDDR+0x80
	MOV WORD[THISF_ADR+GDTable], 8*0x10-1;BX
	LGDT [THISF_PH+GDTable]
	MOV CX, SegTSS
	LTR CX
	ADD WORD[EAX+2], 1
; Load Shell32
	MOV WORD[THISF_ADR+TSSCrt], 8*0x11
	MOV WORD[THISF_ADR+TSSMax], 8*0x11
	MOV ESI, THISF_ADR+msg_load_shell32
	MOV EDI, RotPrint
	CALL SegGate:0
	PUSH DWORD 20
	CALL F_TSSStruct3
; Load Subapp a and b
	PUSH DWORD 50
	ADD WORD[THISF_ADR+TSSMax], 2*8; {LDT+TSS}*8
	CALL F_TSSStruct3
	;
	PUSH DWORD 60
	ADD WORD[THISF_ADR+TSSMax], 2*8; {LDT+TSS}*8
	CALL F_TSSStruct3
; Echo User Area
	MOV EDI, RotPrint
	MOV ESI, THISF_ADR+msg_used_mem
	CALL SegGate:0
	MOV EDI, RotEchoDword
	MOV EDX, [THISF_ADR+UsrAllocPtr]
	CALL SegGate:0
	MOV EDI, RotPrint
	MOV ESI, THISF_ADR+msg_newline
	CALL SegGate:0
; Enable Multi-tasks
	;;STI
; Main loop
	MOV ECX, 500
	Shell32_Test:
	CALL 8*0x11:0x00000000; After CLI before you try this
	MOV EDI, RotPrint
	MOV ESI, THISF_ADR+msg
	CALL SegGate:0
	; LOOP Shell32_Test	
; 
	;;HLT
	;;DB 0xE9
	;;DD -6; [32-b] mainloop: HLT, JMP mainloop

; Restore information then back to real-16 mode
	CLI
	MOV EDI, RotPrint
	MOV ESI, THISF_ADR+msg_quit_32bit
	CALL SegGate:0
	MOV BX, [THISF_ADR+para_ds]
	;
	MOV AX, SegData
	MOV ES, AX
	MOV FS, AX
	MOV GS, AX
	AND DWORD[0x80007090+4], 0x0FBFFFFF; Code Segment
	AND DWORD[0x80007098+4], 0x0F30FFFF; Stak Segment
	AND DWORD[0x80007098+0], 0xFFFF0000; Stak Segment
	MOV EAX, SegStack
	MOV SS, EAX
	JMP DWORD SegCode:Realto
[BITS 16]
	Realto:
	MOV EAX, CR0
	AND EAX, 0x7FFFFFFE; clear PE bit and PG bit
	MOV CR0, EAX
	JMP WORD SEG16_LOD:quit; 16-bit instruction	
erro:;{TODO}
	MOV EDI, RotPrint
	MOV ESI, THISF_ADR+msg_error
	CALL SegGate:0
quit:
; Return to real-16 mode
	; Initialize
	MOV DS, BX
	XOR BX, BX
	MOV ES, BX
	MOV BX, [para_ss]
	MOV SS, BX
	MOV SP, [para_sp]
	Addr20Disable; yo osdev.a
	; Return to base environment
	RETF

;[Procedure real-16 mode]
F_GDTDptrStruct:; Structure Segment Selector
	%define _BSWAP_ALLOW_NOT
	GDTDptrStruct EAX,EBX,ECX
	RET

;[Parameters]
	SvpAllocPtr: DD 0x0000A000; Supervisor
	UsrAllocPtr: DD 0x00100000
	TSSNumb: DW 1
		DD 0; of TSSCrt
	TSSCrt: DW 0; Kernel's=0

	TSSMax: DW 0
	para_cs: DW 0
	para_ds: DW 0
	para_ss: DW 0
	para_sp: DW 0
	GDTPTR: DD 0
	GDTDptr:
	GDTable:
		DW 0
		DD GDT_LDDR
	IVTDptr:
		DW 0
		DD IDT_ADDR
;[Data.Strings]
	msg_spaces: DB "        ",0
	msg_used_mem: DB "Used Memory: ",0
	msg_newline: DB 10,13,0
	;
	msg: DB "Ciallo, Mecocoa~",10,13,0
	msg_paging: DB "Paging initializing...",10,13,0
	msg_setup_basic_gdt: DB "Setting up basic GDT...",10,13,0
	msg_enter_32bit: DB "Enter 32-bit protected mode...",10,13,0
	msg_load_ivt: DB "Load IVT...",10,13,0
	msg_make_base_task: DB "Make base task...",10,13,0
	msg_load_shell32: DB "Load Shell32...",10,13,0
	msg_quit_32bit: DB "Quit 32-bit protected mode...",10,13,0
	;
	msg_on_1s: DB "<Ring~> ",0
	msg_error: DB "Error!",10,13,0
	msg_general_exception: DB "General Exception!",10,13,0
[BITS 32]
%include "kerrout32.a"; 32-bit kernel routines

Endf
Endfile:
