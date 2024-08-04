; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

%include "mecocoa/kernel.inc"

[BITS 32]
GLOBAL _start

section .text
_start:
	MOV EDI, RotInitialize
	CALL SegGate|3:0
	MOV ESI, str0
	MOV EDI, RotPrint
	CALL SegGate|3:0
	MOV EDI, RotTerminal
	CALL SegGate|3:0
_reentry:
	MOV ESI, str1
	MOV EDI, RotPrint
	CALL SegGate|3:0
	MOV ECX, 500; 500ms
	MOV EDI, RotSysDelay
	CALL SegGate|3:0
	; JMP $
	JMP _reentry

section .data
str0: db "(A)",0
str1: db "A",0

