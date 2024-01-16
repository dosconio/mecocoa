; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; Enter 32-bit protected mode and print a message
;{TODO RENAME} Kernel32.asm

; will be loaded at 0x2000

%include "pseudo.a"
%include "osdev.a"
%include "debug.a"

SEG16_GDT EQU 0x0708
STACK_LEN EQU 0x1000
GDT_ADDR  EQU 0x7080


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
MOV ESP, STACK_LEN

MOV BYTE[ES:0], '~'

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
msg_quit_32bit: DB "Quit 32-bit protected mode...",10,13,0
Endf
Endfile:
