; ASCII NASM0207 TAB4 LF
; Attribute: CPU(x86) File(HerELF)
; LastCheck: 20240219
; AllAuthor: @dosconio
; ModuTitle: Paging Module
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

; May exist a paging.c(pp)

%include "cokasha/kernel.inc"

GLOBAL PAGE_INIT

[CPU 386]

[BITS 16]

PAGE_INIT:
	; Clear GPDT to make use of Protect
	XOR EAX, EAX
	MOV CX, 1024; GPDT_NUM (1024*4=4KB)
	MOV DI, PDT_ADDR
	REP STOSD
	; last - Index GPDT self
	MOV DWORD[PDT_ADDR+0x1000-4], PDT_ADDR|3; US=U RW P 00005000~00005FFF FFFFF000~FFFFFFFF
	; Global PgTable
	MOV DWORD[PDT_ADDR+0x1000/2], KPT_ADDR|3; US=S RW P 00008000~00008FFF 80008000~80008FFF
	; Transition also as
	MOV DWORD[PDT_ADDR], KPT_ADDR|7
	; Fill Bitmap
		PUSH ES
		MOV AX, SEGM_BITMAP
		MOV ES, AX
		XOR DI, DI
		; Kernel Area
		MOV CX, 16/4
		XOR EAX, EAX
		REP STOSD
		MOV BYTE[ES:0], 0b11111111
		MOV BYTE[ES:1], 0b00000011
		; BIOS Area
		MOV CX, 16/4
		NOT EAX; 0xFFFF
		REP STOSD
		; User Area
		MOV CX, 16*6/4
		NOT EAX; 0
		REP STOSD
		; Omit the higher (00400000~FFFFFFFF)
		POP ES
	;{TEMP} Fill Kernel PgTable
	MOV DI, KPT_ADDR
	;;  MOV CX, 0x100; 0~0x100'000 , last one: 0x83fc:0x000ff003
	;;  MOV EAX, 3; US=S RW P
	;;  loop_fill_kpt:
	;;  	STOSD
	;;  	ADD EAX, 0x1000; 4MB
	;;  	LOOP loop_fill_kpt
	MOV ECX, 0x10; 0~0xFFFF , last one: 0x83fc:0x000ff003
	MOV EAX, 3; US=S RW P
	loop_fill_01:
		STOSD
		ADD EAX, 0x1000
		LOOP loop_fill_01
	MOV ECX, 0x100-0x10 ;{TEMP} 0x10 000 ~ 0x100 000 
	OR EAX, 7; US=U RW P
	loop_fill_02:
		STOSD
		ADD EAX, 0x1000
		LOOP loop_fill_02

	; Apply
	MOV EAX, PDT_ADDR
	MOV CR3, EAX
RET
