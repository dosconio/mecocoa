; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 64-bit (IA32-e) mode

;%include "mecocoa/kernel.inc"

[BITS 64]
GLOBAL main

section .text
main:
	lup: jmp lup

section .data
tmp: dd 0

