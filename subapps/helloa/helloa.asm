; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

%include "mecocoa/kernel.inc"

[BITS 32]
GLOBAL _start
EXTERN _sysouts
EXTERN _sysquit

section .text
_start:
	MOV ESI, str0
_reentry:
	MOV EDI, RotPrint
	CALL SegGate|3:0
	MOV EDI, RotTerminal
	CALL SegGate|3:0
	MOV ESI, str1
	JMP _reentry

section .data
str0: db "(HelloA)",0
str1: db "[HelloA]",0

