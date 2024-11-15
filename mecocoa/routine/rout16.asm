; ASCII NASM TAB4 CRLF
; Attribute: CPU(x86)
; LastCheck: 20240211
; AllAuthor: @dosconio
; ModuTitle: 16-bit Routine for Kernel
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

GLOBAL Dbg_Func
GLOBAL ReturnModeProt32
GLOBAL Routint16

EXTERN ReadFileFFAT12

%include "video.a"
%include "debug.a"
[CPU 386]
[BITS 16]


section .text
Dbg_Func:
	MOV SI, str_test_message
	MOV AH, 0
	INT 80H
    RET

ReturnModeProt32:
	;{TODO} Store and Restore
	JMP FAR [0x0500]

; ---- ROUTINE 80H ----
Routint16:
	CMP AH, (_r16_prints-_r16_table)/2
	JAE _r16__none
	MOVZX BX, AH
	SHL BX, 1
	JMP NEAR [BX+_r16_table]
_r16__none:
	IRET
_r16_table:
	DW _r16_prints
	DW _r16_printi32
	DW _r16_floloadfat
;      ---- 00 Print String without attribute ---- << SI
_r16_prints:
	PUSHAD
	PUSH ES
	MOV AX, 0xB800
	MOV ES, AX
	ConPrint SI, ~
	POP ES
	POPAD
	IRET
_r16_printi32:
;      ---- 01 Print Integer u32 without attribute ---- << EDX
	PUSHAD
	PUSH ES
	MOV AX, 0xB800
	MOV ES, AX
	PUSH EDX
	ConCursor; Volatile{DX}
	POP EDX
	SHL EAX, 1
	MOVZX EAX, AX
	DbgEcho32 EDX, EAX
	SHR EAX, 1
	ADD AX,8
	ConCursor AX
	POP ES
	POPAD
	IRET
_r16_floloadfat:
;      ---- 02 Load File from FAT12 Floppy ---- << DS:SI(Filename-addr), ES:DI(Dest-addr)
	PUSHAD
	PUSH ES
	PUSH DS
	MOV EAX, EDI
	SHR EAX, 4
	MOV ES, AX
	XOR AX, AX
	MOV DS, AX
	PUSH 0x800; Mecocoa Buffer
	PUSH SI
	PUSH WORD 0
	CALL ReadFileFFAT12
	ADD SP, 2*3
	POP DS
	POP ES
	POPAD
	IRET

section .data
str_test_message:
	DB "Kernel 16: Trying to enter Flat-32...", 10, 13, 0
