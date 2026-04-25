; Mecocoa by @dosconio
; Opensrc under BSD 3-Clause License
;
; subprogram of Shell in 64-bit (IA32-e) mode

;%include "mecocoa/kernel.inc"

SYSC_OUTC EQU 0x80000000
SYSC_EXIT EQU 0x80000002

[BITS 64]
GLOBAL main

section .text
main:
	push rax
	mov rdi, 'Q'
	call outc
	mov rdi, 'w'
	call outc
	mov rdi, 'Q'
	call outc
	mov rdi, 0x0D000721
	mov eax, SYSC_EXIT
	syscall
	lup: jmp lup; unreachable

outc:; syscall; RIP->RCX,FLAG->R11; SystemV&Linux RDI RSI RDX RCX->R10 R8 R9
	mov eax, SYSC_OUTC; OUTC(CHR)
	mov rsi, 0
	mov r10, rcx
	syscall
ret

test_de:
	xor rcx, rcx
	div rcx; #DE

section .data
tmp: dd 0

