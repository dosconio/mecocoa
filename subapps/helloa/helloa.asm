; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

%include "mecocoa/kernel.inc"

[BITS 32]
EXTERN _entry
GLOBAL _main

section .text
_main:
	; [Print a string]
	 MOV ESI, str1
	 MOV EDI, RotPrint
	 CALL SegGate|3:0
	; [Delay in ms]
	MOV ECX, 5000; 5 seconds
	MOV EDI, RotSysDelay
	CALL SegGate|3:0
	MOV EAX, 0
	RET

section .data
str1: db "(A)",0

