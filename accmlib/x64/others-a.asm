
[BITS 64]

GLOBAL syscall

section .text

syscall:
	; RDI RSI RDX RCX -> RAX RDI RSI RDX (all are caller-saved)
	MOV RAX, RDI
	MOV RDI, RSI
	MOV RSI, RDX
	MOV RDX, RCX
	SYSCALL
RET

