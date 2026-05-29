; ASCII NASM0207 TAB4 CRLF
; Attribute: CPU(x86)
; AllAuthor: @ArinaMgk
; ModuTitle: User-lib of Mecocoa
; Copyright: Dosconio Mecocoa, BSD 3-Clause License

[CPU 586]
;%include "mecocoa/kernel.inc"

GLOBAL _start
EXTERN main, _exit
EXTERN _preprocess, _init_environ

section .text

_start:
	xor	ebp, ebp		; Mark the end of stack frames
	pop	eax				; Get argc from stack, ESP now points to argv
	mov	ebx, esp		; Get argv pointer
	lea	ecx, [ebx + eax*4 + 4] ; Get envp pointer (argv + argc*4 + 4 for NULL)
	and	esp, -16		; Ensure 16-byte alignment
	push	ecx			; Third argument: envp
	push	ebx			; Second argument: argv
	push	eax			; First argument: argc

	call _init_environ
	call _preprocess


	call	main		; Call main(argc, argv, envp)
	add	esp, 16			; Clean up arguments and padding
	push	eax			; Push return value for exit
	call	_exit		; Call exit(status)
mov byte[0], 0

section .note.GNU-stack noalloc noexec nowrite progbits

