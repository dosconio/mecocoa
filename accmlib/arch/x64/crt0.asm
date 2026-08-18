; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x64)
; AllAuthor: @ArinaMgk
; ModuTitle: User-lib of Mecocoa
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

[BITS 64]
;%include "mecocoa/kernel.inc"

GLOBAL _start
EXTERN main, _exit
EXTERN _preprocess, _init_environ

section .text

_start:
	xor	rbp, rbp		; Mark the end of stack frames
	pop	rdi				; Get argc from stack, RSP now points to argv
	mov	rsi, rsp		; Get argv pointer
	lea	rdx, [rsi + rdi*8 + 8] ; Get envp pointer (argv + argc*8 + 8 for NULL)
	and	rsp, -16		; Ensure 16-byte alignment

	; Save arguments to callee-saved registers
	mov	r12, rdi
	mov	r13, rsi
	mov	r14, rdx

	call	_init_environ
	call	_preprocess

	; Restore arguments for main
	mov	rdi, r12
	mov	rsi, r13
	mov	rdx, r14

	call	main		; Call main(argc, argv, envp)
	mov	rdi, rax		; Pass return value to exit
	call	_exit		; Call exit(status)
mov byte[0], 0

section .note.GNU-stack noalloc noexec nowrite progbits
