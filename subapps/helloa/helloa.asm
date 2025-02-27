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
	; [Delay 10s]
	loop0:
		MOV EAX, DWORD[0x518]
		ADD EAX, 100
		CMP EAX, 100
		JB loop0
	MOV [tmp], EAX
	loops:
		MOV EAX, DWORD[0x518]
		CMP EAX, [tmp]
		JB loops
	JMP main
MOV EAX, 0
RET

section .data
tmp: dd 0

