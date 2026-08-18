[BITS 64]

GLOBAL syscall, __sigrestorer

section .text

syscall:
	; RDI RSI RDX RCX -> RAX RDI RSI RDX (all are caller-saved)
	MOV RAX, RDI
	MOV RDI, RSI
	MOV RSI, RDX
	MOV RDX, RCX
	SYSCALL
RET

__sigrestorer:
	MOV RAX, 0x15 ; syscall_t::SIGR (0x15)
	SYSCALL
