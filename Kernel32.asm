; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
; - loaded at `0x2000`
; Enter 32-bit protected mode and print a message

%include "pseudo.a"
%include "osdev.a"
%include "debug.a"
%include "video.a"
%include "hdisk.a"

; {Option Switch: 1 or others}
IVT_TIMER_ENABLE EQU 1
MEM_PAGED_ENABLE EQU 1

%include "offset.a" ; addresses and selectors
%include "routine.a"; index of routines

[CPU 386]
File

CLI

MOV [para_cs], CS
MOV [para_ds], DS
MOV WORD[quit_addr], quit

MOV SI, msg
MOV AH, 0
INT 80H

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
		MOV BYTE[ES:DI], 0xC0
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
		; GDTE 04 Video display buffer 32K - 000B8000~000BFFFF (32KB) Ring(0)
		GDTDptrAppend 0x0040920B,0x80007FFF
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

[BITS 32];--------------------------------
mainx:
; Load segment registers
	MOV EAX, SegData
	MOV DS, EAX
	MOV EAX, SegVideo
	MOV ES, EAX
	MOV EAX, SegStack
	MOV SS, EAX
	XOR ESP, ESP
; Trifle
	; Transition for. Now remove it.
	;MOV DWORD[PDT_LDDR], 0
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
	%if IVT_TIMER_ENABLE==1
	; Setup timer
	GateStruct Timer_70HINTHandler,SegCode,0x8E00; 32BIT R0
	MOV [IDT_ADDR+8*0x70], EAX
	MOV [IDT_ADDR+8*0x70+4], EDX
	; Load IDT
	SUB ESI, IDT_ADDR+1
	MOV EAX, ESI
	MOV [THISF_ADR+IVTDptr], AX
	LIDT [THISF_ADR+IVTDptr]
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

STI
mainloop: HLT
	JMP mainloop
DbgStop

; Restore information then back to real-16 mode
	;{TODO} Print quit message
	MOV BX, [para_ds]
	;
	MOV EAX, CR0
	AND EAX, 0x7FFFFFFE; clear PE bit and PG bit
	MOV CR0, EAX
	;
	MOV DS, BX
	;
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
	DD GDT_LDDR
msg: DB "Ciallo, Mecocoa~",10,13,0
msg_paging: DB "Paging initializing...",10,13,0
msg_setup_basic_gdt: DB "Setting up basic GDT...",10,13,0
msg_return_to_real16: DB "Return to real-16 mode...",10,13,0
msg_enter_32bit: DB "Enter 32-bit protected mode...",10,13,0
msg_load_ivt: DB "Load IVT...",10,13,0


msg_quit_32bit: DB "Quit 32-bit protected mode...",10,13,0

;
msg_on_1s: DB "<Ring~> ",0
;{TODO}
GDTDptr: DW 0
	DD 0
IVTDptr: DW 0
	DD IDT_ADDR

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
R_Print:
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

;[Interrupts protect-32 mode]
Timer_70HINTHandler:
	PUSHAD
	PUSH DS
	MOV AX, SegData
	MOV DS, EAX
	;{ISSUE} Why these codes make interrupt only once? --@dosconio 2024/Jan/17
		; PUSH WORD SegData
		; POP DS
	MOV ESI, THISF_ADR+msg_on_1s
	MOV EDI, 1
	CALL SegGate:0
	POP DS
	;
	MOV AL, 0x20; EOI
	OUT 0xA0, AL; SLAVE
	OUT 0x20, AL; MASTER
	; OPEN NMI AFTER READ REG-C, OR ONLY INT ONCE
	MOV AL, 0X0C
	OUT 0X70, AL
	IN AL, 0x71
	POPAD
	IRETD
GeneralExceptionHandler:
	;{TODO} Print exception message
	DbgStop
GeneralInterruptHandler:
	PUSH EAX
	MOV AL, 0x20; EOI
	OUT 0xA0, AL; SLAVE
	OUT 0x20, AL; MASTER
	POP EAX
	IRETD

Endf
Endfile:
