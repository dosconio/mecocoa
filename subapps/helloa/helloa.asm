; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 32-bit protected mode

;%include "mecocoa/kernel.inc"

[BITS 32]
GLOBAL main

section .text
main:
	MOV EAX, DWORD[0x518]
	MOV [tmp], EAX
	; [Print Char]
	MOV DWORD[0x500], 0x00
	MOV DWORD[0x504], 'A'
	INT 0x81; CALL 8*3|3:0
	; [Delay 2s]
	loop0:
		MOV EAX, [0x518]
		SUB EAX, [tmp]
		CMP EAX, 2
		JB loop0
	JMP main
MOV EAX, 0
RET

section .data
tmp: dd 0

