; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 64-bit (IA32-e) mode

;%include "mecocoa/kernel.inc"

[BITS 64]
GLOBAL main

section .text
main:
	mov rdi, 'Q'
	call test_syscall
	mov rdi, 'w'
	call test_syscall
	mov rdi, 'Q'
	call test_syscall
	lup: jmp lup

test_syscall:; syscall; RIP->RCX,FLAG->R11; SystemV&Linux RDI RSI RDX RCX->R10 R8 R9
	mov eax, 0x80000000; OUTC(CHR)
	mov r10, rcx
	syscall
ret

test_de:
	xor rcx, rcx
	div rcx; #DE

section .data
tmp: dd 0

