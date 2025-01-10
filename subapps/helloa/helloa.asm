; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

;%include "mecocoa/kernel.inc"

[BITS 32]
GLOBAL main

section .text
main:
	; [Print Char]
	MOV DWORD[0x500], 0x00
	MOV DWORD[0x504], 'A'
	CALL 8*3|3:0
	; [Delay 1000ms]
	MOV EAX, DWORD[0x518]
	MOV [tmp], EAX
	loops:
	MOV EAX, DWORD[0x518]
	CMP EAX, [tmp]
	JZ loops
		JMP main
MOV EAX, 0
RET

section .data
tmp: dd 0

