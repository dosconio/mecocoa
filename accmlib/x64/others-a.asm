
[BITS 64]

GLOBAL syscall, _start
EXTERN main, exit

section .text

syscall:
	; RDI RSI RDX RCX -> RAX RDI RSI RDX (all are caller-saved)
	MOV RAX, RDI
	MOV RDI, RSI
	MOV RSI, RDX
	MOV RDX, RCX
	SYSCALL
RET

_start:
	xor	rbp, rbp	; Mark the end of stack frames
	pop	rdi			; Get argc from stack, RSP now points to argv
	mov	rsi, rsp	; Get argv pointer
	and	rsp, -16	; Ensure 16-byte alignment for System V ABI
	call	main	; Call main(argc, argv)
	mov	rdi, rax	; Use return value as exit code
	call	exit	; Call exit(status)
mov byte[0], 0
