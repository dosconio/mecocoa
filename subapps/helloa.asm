; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

%include "kernel.inc"

[BITS 32]
GLOBAL _start
GLOBAL _sysouts
GLOBAL _sysquit

section .text
_start:
	MOV ESI, str0
	MOV EDI, RotPrint
	CALL SegGate|3:0
	MOV EDI, RotTerminal
	CALL SegGate|3:0
	JMP _start

_sysouts:
	PUSH EBP
	MOV EBP, ESP
	PUSH ESI
	PUSH EDI
	MOV ESI, [EBP+4*2]
	MOV EDI, RotPrint
	CALL SegGate|3:0
	POP EDI
	POP ESI
	MOV ESP, EBP
	POP EBP
RET

_sysquit:
	MOV EDI, RotTerminal
	CALL SegGate|3:0
RET

section .data
str0: db "[HelloA]",0


